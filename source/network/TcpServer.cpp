#include "TcpServer.h"
#include "channel/EventType.h"
#include "error/errorUtility.h"
#include "fmt/core.h"
#include <algorithm>
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <functional>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <netinet/tcp.h>
#include <ostream>
#include <squid.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility/squidUtility.h>
#include <vector>
using namespace squid;
void squid::TcpServer::Run()
{
    if (listenFd == -1)
    {
        return;
    }
    fmt::print("Begin Run\n");
    baseEventLoop->Loop();
}
void squid::TcpServer::SetSocketOption(int option, bool enable)
{
    if (listenFd == -1)
    {
        return;
    }
    setsockopt(listenFd, SOL_SOCKET, option, &enable, sizeof(enable));
}
void squid::TcpServer::Bind(int port)
{
    struct addrinfo hints, *res;
    auto tmp = 0;
    bzero(&hints, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    // hints.ai_socktype = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
    hints.ai_socktype = SOCK_STREAM;
    if (tmp = getaddrinfo(nullptr, std::to_string(port).c_str(), &hints, &res); tmp != 0)
    {
        ErrorUtility::LogError(SocketError::GetAddrInfo);
        return;
    }
    std::shared_ptr<addrinfo> sharedRes{res};
    res->ai_socktype |= (SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (listenFd = socket(res->ai_family, res->ai_socktype, res->ai_protocol); listenFd == -1)
    {
        ErrorUtility::LogError(SocketError::CreateSocket);
        return;
    }
    SetSocketOption(listenFd, SO_REUSEPORT);
    SetSocketOption(listenFd, SO_REUSEADDR);
    if (tmp = bind(listenFd, res->ai_addr, res->ai_addrlen); tmp != 0)
    {
        ErrorUtility::LogError(SocketError::BindSocket);
        return;
    }
    if (tmp = listen(listenFd, 512); tmp != 0)
    {
        ErrorUtility::LogError(SocketError::ListenScoket);
        return;
    }
    connectionHandler->EnableReadEvent(true);
    connectionHandler->RegisterEvent(std::bind(&TcpServer::BuildNewConnection, this, std::placeholders::_1),
                                     EventType::Read);
    baseEventLoop->RunOnceInLoop([this]() { baseEventLoop->RegisterEventHandler(connectionHandler, listenFd); });

    fmt::print("Bind succ\n");
}

TcpServer::~TcpServer()
{
    if (listenFd != -1)
    {
        close(listenFd);
    }
    if (epollFd != -1)
    {
        close(epollFd);
    }
}
void TcpServer::BuildNewConnection(int fd)
{
    struct sockaddr_in clientAddr;
    auto size = sizeof(clientAddr);
    auto connFd =
        accept(listenFd, reinterpret_cast<struct sockaddr *>(&clientAddr), reinterpret_cast<socklen_t *>(&size));
    if (connFd == -1)
    {
        ErrorUtility::LogError(SocketError::AcceptSocket);
        return;
    }
    auto loop = _eventLoopThreadPool.GetLoop();
    std::shared_ptr<Connection> connection(new Connection(clientAddr, fd, loop));
    for (auto &it : _messageSendEvent)
    {
        connection->RegisterMessageSendEvent(it);
    }
    for (auto &it : _messageReceiveEvent)
    {
        connection->RegisterMessageReceiveEvent(it);
    }
    connectionMap[fd] = connection;
    OnConnectionAccept(*connection);
}
void TcpServer::CloseConnection(int fd)
{
}
TcpServer::TcpServer(int threadCount)
    : threadCount(threadCount), connectionHandler(new EventHandler), _eventLoopThreadPool(baseEventLoop), listenFd(-1),
      epollFd(-1), baseEventLoop(new EventLoop)
{
}

void TcpServer::OnConnectionAccept(Connection &connection)
{
    for (auto &it : _connectAcceptEvents)
    {
        it(connection);
    }
}

void TcpServer::OnConnectionClose(Connection &connection)
{
    for (auto &it : _connectCloseEvents)
    {
        it(connection);
    }
}
void TcpServer::RegisterMessageSendEvent(VoidEvent event)
{
    _messageSendEvent.emplace_back(event);
}

void TcpServer::RegisterMessageReceiveEvent(MessageEvent event)
{
    _messageReceiveEvent.emplace_back(event);
}
void TcpServer::RegisterConnectionAcceptEvent(ConnectionEvent event)
{
    _connectAcceptEvents.emplace_back(event);
}
void TcpServer::RegisterConnectionCloseEvent(ConnectionEvent event)
{
    _connectCloseEvents.emplace_back(event);
}
