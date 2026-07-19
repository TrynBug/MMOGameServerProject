#pragma once

#include "pch.h"
#include "ShardedThreadSafeUnorderedMap.h"
#include "ThreadSafeUnorderedMap.h"
#include "User.h"

class GameServer;
class PacketSender;

// GameServer 범위 이상의 채팅과 CommunicationServer 연동을 담당한다.
// Stage 채널 채팅은 Stage가 자신의 유저 목록을 사용해 직접 처리한다.
class ChatManager
{
public:
    ChatManager(GameServer& server,
                ShardedThreadSafeUnorderedMap<int64, UserPtr>& safeUsers,
                SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr>& safeCommunicationSessions,
                PacketSender& packetSender);
    ~ChatManager() = default;

    ChatManager(const ChatManager&) = delete;
    ChatManager& operator=(const ChatManager&) = delete;

    // Stage에서 호출하는 채팅 송신과 presence 변경 진입점.
    void BroadcastGameServerChat(int64 senderCharacterId, const std::string& senderName, const std::string& message);
    void RequestGlobalChat(int64 senderCharacterId, const std::string& senderName, const std::string& message);
    void NotifyPresence(int64 characterId, const std::string& characterName, bool online);
    void RequestWhisper(int64 senderAccountId, int64 senderCharacterId, const std::string& senderName,
                        const std::string& targetName, const std::string& message);

    // CommunicationServer handshake 응답 직후 현재 활성 캐릭터를 일괄 전송한다.
    void SendPresenceSnapshot(const netlib::ISessionPtr& spSession, int32 communicationServerId);

    // CommunicationServer에서 받은 채팅 알림과 결과를 로컬 유저에게 전달한다.
    void HandleCommunicationChatBroadcastNtf(const netlib::ISessionPtr& spSession, const ServerPacket::ChatBroadcastNtf& msg);
    void HandleCommunicationWhisperNtf(const netlib::ISessionPtr& spSession, const ServerPacket::WhisperNtf& msg);
    void HandleCommunicationWhisperRes(const netlib::ISessionPtr& spSession, const ServerPacket::WhisperRes& msg);

private:
    void broadcastGlobalChat(int64 senderCharacterId, const std::string& senderName, const std::string& message);

    static InternalSessionMeta* getInternalSessionMeta(const netlib::ISessionPtr& spSession);

    // GameServer의 직렬화 기능과 thread-safe 유저/세션 저장소를 참조한다.
    GameServer& m_server;
    ShardedThreadSafeUnorderedMap<int64, UserPtr>& m_safeUsers;
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr>& m_safeCommunicationSessions;
    PacketSender& m_packetSender;

    // CommunicationServer 연결 여부와 무관하게 현재 선택된 활성 캐릭터 presence를 유지한다.
    SharedThreadSafeUnorderedMap<int64, std::string> m_safePresences;
};
