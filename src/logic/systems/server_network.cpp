#include "server_network.h"

#include <iostream>
#include "logic/components/client_connection.h"
#include <util/util.h>

ServerNetworkSys::ServerNetworkSys(entt::registry& reg) : System(reg) {
    
    SteamNetworkingIdentity identity;
    identity.Clear();
    SteamNetworkingErrMsg error;

    if(!GameNetworkingSockets_Init(&identity, error)) {
        std::cout << "EH FRER FAIT GAFFE YA UNE ERREUR DE RÉSEAU" << error << std::endl;
    }
    net = SteamNetworkingSockets();
    SteamNetworkingIPAddr addr;
    addr.Clear();
    addr.SetIPv6LocalHost(25565);
    socket = net->CreateListenSocketIP(addr);

}

void ServerNetworkSys::preinit() {
    
}

void ServerNetworkSys::init() {
    
}

uint32_t index_jaipasenviedecmetijeorj = 0;
void ServerNetworkSys::tick() {
    net->RunCallbacks(this);
    auto view = reg.view<ClientConnection>();
    const int MESSAGE_BUFFER = 10;
    std::vector<SteamNetworkingMessage_t*> messages(MESSAGE_BUFFER);
    for(auto entity : view) {
        auto& connection = view.get(entity).connection;
        int received = net->ReceiveMessagesOnConnection(connection, messages.data(), MESSAGE_BUFFER);
        if(received == -1) {
            Util::log(Util::error) << "OOF CAN'T RECEIVE ÙESSAGES" << std::endl;
        } else {
            for(int i = 0; i < received; ++i) {
                Util::log() << (char*) messages[i]->m_pData << std::endl;
            }
        }
        net->SendMessageToConnection(connection, &index_jaipasenviedecmetijeorj, sizeof(uint32_t), k_nSteamNetworkingSend_Reliable);
    }
    ++index_jaipasenviedecmetijeorj;
}

void ServerNetworkSys::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* inf) {
    
    if(inf->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting) {
        std::cout << "connecting " << inf->m_info.m_addrRemote.GetIPv4() << std::endl;
        net->AcceptConnection(inf->m_hConn);
        entt::entity entity = reg.create();
        auto& comp = reg.assign<ClientConnection>(entity);
        comp.connection = inf->m_hConn;
    } else if(inf->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
        std::cout << "connected " << inf->m_info.m_addrRemote.GetIPv4() << std::endl;
    } else {
        std::cout << inf->m_info.m_eState << " " << inf->m_info.m_addrRemote.GetIPv4() << std::endl;
    }
    
}

void ServerNetworkSys::finish() {
    
}

ServerNetworkSys::~ServerNetworkSys() {
    
    net->CloseListenSocket(socket);
    GameNetworkingSockets_Kill();
    
}
