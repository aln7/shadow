/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shd-tgen.h"

struct _TGenServer {
    TGenServer_onNewPeerFunc notify;
    gpointer notifyData;

    gint socketD;

    gint refcount;
    guint magic;
};

static void _tgenserver_acceptPeer(TGenServer* server) {
    TGEN_ASSERT(server);

    /* we have a peer connecting to our listening socket */
    struct sockaddr_in peerAddress;
    memset(&peerAddress, 0, sizeof(struct sockaddr_in));
    socklen_t addressLength = (socklen_t)sizeof(struct sockaddr_in);

    gint peerSocketD = accept(server->socketD, (struct sockaddr*)&peerAddress, &addressLength);
    if(peerSocketD >= 0) {
        TGenPeer* peer = tgenpeer_new(peerAddress.sin_addr.s_addr, peerAddress.sin_port);
        server->notify(server->notifyData, peerSocketD, peer);
        tgenpeer_unref(peer);
    } else {
        tgen_critical("accept(): socket %i returned %i error %i: %s",
                server->socketD, peerSocketD, errno, g_strerror(errno));
    }
}

static TGenEvent _tgenserver_onEvent(TGenServer* server, gint descriptor, TGenEvent events) {
    TGEN_ASSERT(server);

    g_assert((events & TGEN_EVENT_READ) && descriptor == server->socketD);
    _tgenserver_acceptPeer(server);

    /* we will only ever accept and never write */
    return TGEN_EVENT_READ;
}

TGenServer* tgenserver_new(TGenIO* io, in_port_t serverPort, TGenServer_onNewPeerFunc notify, gpointer notifyData) {
    /* we run our protocol over a single server socket/port */
    gint socketD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socketD <= 0) {
        tgen_critical("socket(): returned %i error %i: %s", socketD, errno, g_strerror(errno));
        return NULL;
    }

    /* setup the listener address information */
    struct sockaddr_in listener;
    memset(&listener, 0, sizeof(struct sockaddr_in));
    listener.sin_family = AF_INET;
    listener.sin_addr.s_addr = htonl(INADDR_ANY);
    listener.sin_port = serverPort;

    /* bind the socket to the server port */
    gint result = bind(socketD, (struct sockaddr *) &listener, sizeof(listener));
    if (result < 0) {
        tgen_critical("bind(): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }

    /* set as server listening socket */
    result = listen(socketD, SOMAXCONN);
    if (result < 0) {
        tgen_critical("listen(): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }

    /* if we got here, everything worked correctly! */
    gchar ipStringBuffer[INET_ADDRSTRLEN + 1];
    memset(ipStringBuffer, 0, INET_ADDRSTRLEN + 1);
    inet_ntop(AF_INET, &listener.sin_addr.s_addr, ipStringBuffer, INET_ADDRSTRLEN);
    tgen_message("server listening at %s:%u", ipStringBuffer, ntohs(listener.sin_port));

    /* allocate the new server object and return it */
    TGenServer* server = g_new0(TGenServer, 1);
    server->magic = TGEN_MAGIC;
    server->refcount = 1;

    server->notify = notify;
    server->notifyData = notifyData;

    server->socketD = socketD;

    /* make sure to manage events */
    tgenio_register(io, socketD, (TGenIO_onEventFunc) _tgenserver_onEvent, server);

    return server;
}

static void _tgenserver_free(TGenServer* server) {
    TGEN_ASSERT(server);
    g_assert(server->refcount == 0);

    if(server->socketD > 0) {
        close(server->socketD);
    }

    server->magic = 0;
    g_free(server);
}

void tgenserver_ref(TGenServer* server) {
    TGEN_ASSERT(server);
    server->refcount++;
}

void tgenserver_unref(TGenServer* server) {
    TGEN_ASSERT(server);
    if(--(server->refcount) <= 0) {
        _tgenserver_free(server);
    }
}