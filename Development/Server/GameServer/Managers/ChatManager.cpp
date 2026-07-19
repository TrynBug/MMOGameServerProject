#include "pch.h"
#include "Managers/ChatManager.h"
#include "GameServer.h"
#include "PacketSender.h"
#include "StageObjects/Character.h"

ChatManager::ChatManager(GameServer& server,
                         ShardedThreadSafeUnorderedMap<int64, UserPtr>& safeUsers,
                         SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr>& safeCommunicationSessions,
                         PacketSender& packetSender)
    : m_server(server)
    , m_safeUsers(safeUsers)
    , m_safeCommunicationSessions(safeCommunicationSessions)
    , m_packetSender(packetSender)
{
}

void ChatManager::BroadcastGameServerChat(int64 senderCharacterId, const std::string& senderName, const std::string& message)
{
    // IOCP worker와 Stage 콘텐츠 스레드가 함께 접근하므로 thread-safe map에서 수신자를 수집한다.
    // 캐릭터 선택 화면(None)과 Stage 이동 중(Moving) 유저는 채팅 수신 대상에서 제외한다.
    std::vector<int64> recipientAccountIds;
    recipientAccountIds.reserve(m_safeUsers.Size());
    m_safeUsers.ForEach([&recipientAccountIds](int64 accountId, const UserPtr& spUser)
    {
        if (spUser && spUser->GetStageState() == EUserStageState::InStage)
            recipientAccountIds.push_back(accountId);
    });

    m_packetSender.SendChatRecvNtf(recipientAccountIds, GamePacket::CHAT_TYPE_GAME_SERVER,
                                   senderCharacterId, senderName, message);
}

void ChatManager::RequestGlobalChat(int64 senderCharacterId, const std::string& senderName, const std::string& message)
{
    // 현재 글로벌 채팅 broker는 단일 CommunicationServer로 운영한다.
    // 여러 broker에 같은 요청을 보내면 각 broker의 fan-out이 중복되므로, 구성 오류로 보고 요청을 거절한다.
    const std::vector<int32> communicationServerIds = m_safeCommunicationSessions.CollectKeys(
        [](const int32&, const netlib::ISessionPtr&) { return true; });
    if (communicationServerIds.size() != 1)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GlobalChat rejected: expected one CommunicationServer session, count={}", communicationServerIds.size()));
        return;
    }

    netlib::ISessionPtr spSession;
    if (!m_safeCommunicationSessions.Find(communicationServerIds.front(), spSession) || !spSession)
    {
        LOG_WRITE(LogLevel::Warn, "GlobalChat rejected: CommunicationServer session unavailable.");
        return;
    }

    ServerPacket::ChatBroadcastReq req;
    req.set_sender_character_id(senderCharacterId);
    req.set_sender_name(senderName);
    req.set_message(message);

    netlib::PacketPtr spPacket = m_server.SerializePacket(Common::SERVER_PACKET_ID_CHAT_BROADCAST_REQ, req);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "GlobalChat failed to serialize ChatBroadcastReq.");
        return;
    }

    spSession->Send(spPacket);
}

void ChatManager::NotifyPresence(int64 characterId, const std::string& characterName, bool online)
{
    // 연결이 끊겨도 재연결 snapshot을 만들 수 있도록 활성 캐릭터 상태를 먼저 갱신한다.
    if (online)
        m_safePresences.Insert(characterId, characterName);
    else
        m_safePresences.Erase(characterId);

    const std::vector<int32> communicationServerIds = m_safeCommunicationSessions.CollectKeys(
        [](const int32&, const netlib::ISessionPtr&) { return true; });
    if (communicationServerIds.size() != 1)
    {
        LOG_WRITE(LogLevel::Warn, std::format("ChatPresence send skipped: expected one CommunicationServer session, count={}", communicationServerIds.size()));
        return;
    }

    netlib::ISessionPtr spSession;
    if (!m_safeCommunicationSessions.Find(communicationServerIds.front(), spSession) || !spSession)
    {
        LOG_WRITE(LogLevel::Warn, "ChatPresence send skipped: CommunicationServer session unavailable.");
        return;
    }

    ServerPacket::ChatPresenceNtf ntf;
    ntf.set_character_id(characterId);
    ntf.set_character_name(characterName);
    ntf.set_online(online);

    netlib::PacketPtr spPacket = m_server.SerializePacket(Common::SERVER_PACKET_ID_CHAT_PRESENCE_NTF, ntf);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "ChatPresence failed to serialize ChatPresenceNtf.");
        return;
    }

    spSession->Send(spPacket);
}

void ChatManager::RequestWhisper(int64 senderAccountId, int64 senderCharacterId, const std::string& senderName,
                                 const std::string& targetName, const std::string& message)
{
    const std::vector<int32> communicationServerIds = m_safeCommunicationSessions.CollectKeys(
        [](const int32&, const netlib::ISessionPtr&) { return true; });
    if (communicationServerIds.size() != 1)
    {
        LOG_WRITE(LogLevel::Warn, std::format("Whisper rejected: expected one CommunicationServer session, count={}", communicationServerIds.size()));
        m_packetSender.SendChatSendRes(senderAccountId, EResultCode::Fail, "커뮤니케이션 서버에 연결할 수 없습니다.", GamePacket::CHAT_TYPE_WHISPER,
                                      senderName, targetName, message);
        return;
    }

    netlib::ISessionPtr spSession;
    if (!m_safeCommunicationSessions.Find(communicationServerIds.front(), spSession) || !spSession)
    {
        LOG_WRITE(LogLevel::Warn, "Whisper rejected: CommunicationServer session unavailable.");
        m_packetSender.SendChatSendRes(senderAccountId, EResultCode::Fail, "커뮤니케이션 서버에 연결할 수 없습니다.", GamePacket::CHAT_TYPE_WHISPER,
                                      senderName, targetName, message);
        return;
    }

    // 대상 이름 해석은 전역 presence를 가진 CommunicationServer가 담당한다.
    ServerPacket::WhisperReq req;
    req.set_sender_account_id(senderAccountId);
    req.set_sender_character_id(senderCharacterId);
    req.set_sender_name(senderName);
    req.set_target_name(targetName);
    req.set_message(message);

    netlib::PacketPtr spPacket = m_server.SerializePacket(Common::SERVER_PACKET_ID_WHISPER_REQ, req);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "Whisper failed to serialize WhisperReq.");
        m_packetSender.SendChatSendRes(senderAccountId, EResultCode::Fail, "귓속말 요청 전송에 실패했습니다.", GamePacket::CHAT_TYPE_WHISPER,
                                      senderName, targetName, message);
        return;
    }

    spSession->Send(spPacket);
}

void ChatManager::SendPresenceSnapshot(const netlib::ISessionPtr& spSession, int32 communicationServerId)
{
    // ForEach의 read lock이 유지되는 동안 Stage의 presence 변경은 대기하며, snapshot 뒤에 최신 변경 패킷이 전송된다.
    int32 presenceCount = 0;
    m_safePresences.ForEach([this, &spSession, &presenceCount](int64 characterId, const std::string& characterName)
    {
        ServerPacket::ChatPresenceNtf ntf;
        ntf.set_character_id(characterId);
        ntf.set_character_name(characterName);
        ntf.set_online(true);

        netlib::PacketPtr spPacket = m_server.SerializePacket(Common::SERVER_PACKET_ID_CHAT_PRESENCE_NTF, ntf);
        if (!spPacket)
        {
            LOG_WRITE(LogLevel::Error, std::format("ChatPresence snapshot serialization failed. characterId={}", characterId));
            return;
        }

        spSession->Send(spPacket);
        ++presenceCount;
    });

    LOG_WRITE(LogLevel::Info, std::format("ChatPresence snapshot sent. serverId={} count={}", communicationServerId, presenceCount));
}

void ChatManager::HandleCommunicationChatBroadcastNtf(const netlib::ISessionPtr& spSession, const ServerPacket::ChatBroadcastNtf& msg)
{
    InternalSessionMeta* pMeta = getInternalSessionMeta(spSession);
    if (!pMeta || !pMeta->handshakeDone || pMeta->peerServerType != ServerType::Communication)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GlobalChat notification rejected: invalid CommunicationServer session. sessionId={}", spSession->GetId()));
        spSession->Disconnect();
        return;
    }

    constexpr size_t k_maxChatMessageBytes = 256;
    if (msg.message().empty() || msg.message().size() > k_maxChatMessageBytes)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GlobalChat notification rejected: invalid message size. communicationServerId={} size={}",
            pMeta->peerServerId, msg.message().size()));
        return;
    }

    broadcastGlobalChat(msg.sender_character_id(), msg.sender_name(), msg.message());
}

void ChatManager::HandleCommunicationWhisperNtf(const netlib::ISessionPtr& spSession, const ServerPacket::WhisperNtf& msg)
{
    InternalSessionMeta* pMeta = getInternalSessionMeta(spSession);
    if (!pMeta || !pMeta->handshakeDone || pMeta->peerServerType != ServerType::Communication)
    {
        LOG_WRITE(LogLevel::Warn, std::format("Whisper notification rejected: invalid CommunicationServer session. sessionId={}", spSession->GetId()));
        spSession->Disconnect();
        return;
    }

    constexpr size_t k_maxChatMessageBytes = 256;
    if (msg.sender_character_id() <= 0 || msg.target_character_id() <= 0 || msg.message().empty() || msg.message().size() > k_maxChatMessageBytes)
    {
        LOG_WRITE(LogLevel::Warn, std::format("Whisper notification rejected: invalid message. communicationServerId={} senderCharacterId={} targetCharacterId={} size={}",
            pMeta->peerServerId, msg.sender_character_id(), msg.target_character_id(), msg.message().size()));
        return;
    }

    int64 targetAccountId = 0;
    m_safeUsers.ForEach([&msg, &targetAccountId](int64 accountId, const UserPtr& spUser)
    {
        if (targetAccountId != 0 || !spUser)
            return;

        CharacterPtr spCharacter = spUser->GetCurrentCharacter();
        if (spCharacter && spCharacter->GetProto().character_id() == msg.target_character_id())
            targetAccountId = accountId;
    });

    if (targetAccountId == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("Whisper target user not found. targetCharacterId={}", msg.target_character_id()));
        return;
    }

    // Character ID로 찾은 로컬 계정 한 명에게만 귓속말을 전달한다.
    m_packetSender.SendChatRecvNtf({ &targetAccountId, 1 }, GamePacket::CHAT_TYPE_WHISPER,
                                   msg.sender_character_id(), msg.sender_name(), msg.message(), msg.target_character_id());
}

void ChatManager::HandleCommunicationWhisperRes(const netlib::ISessionPtr& spSession, const ServerPacket::WhisperRes& msg)
{
    InternalSessionMeta* pMeta = getInternalSessionMeta(spSession);
    if (!pMeta || !pMeta->handshakeDone || pMeta->peerServerType != ServerType::Communication)
    {
        LOG_WRITE(LogLevel::Warn, std::format("Whisper response rejected: invalid CommunicationServer session. sessionId={}", spSession->GetId()));
        spSession->Disconnect();
        return;
    }

    if (msg.sender_account_id() <= 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("Whisper response rejected: invalid senderAccountId. communicationServerId={} senderAccountId={}",
            pMeta->peerServerId, msg.sender_account_id()));
        return;
    }

    // CommunicationServer의 라우팅 결과를 송신 클라이언트의 ChatSendRes로 변환한다.
    const EResultCode resultCode = msg.success() ? EResultCode::Success : EResultCode::Fail;
    m_packetSender.SendChatSendRes(msg.sender_account_id(), resultCode, msg.error_msg(), GamePacket::CHAT_TYPE_WHISPER,
                                  msg.sender_name(), msg.target_name(), msg.message());
}

void ChatManager::broadcastGlobalChat(int64 senderCharacterId, const std::string& senderName, const std::string& message)
{
    // fan-out 알림을 Stage 상태와 무관하게 이 GameServer가 보유한 모든 유저에게 배포한다.
    std::vector<int64> recipientAccountIds;
    recipientAccountIds.reserve(m_safeUsers.Size());
    m_safeUsers.ForEach([&recipientAccountIds](int64 accountId, const UserPtr& spUser)
    {
        if (spUser)
            recipientAccountIds.push_back(accountId);
    });

    m_packetSender.SendChatRecvNtf(recipientAccountIds, GamePacket::CHAT_TYPE_GLOBAL,
                                   senderCharacterId, senderName, message);
}

InternalSessionMeta* ChatManager::getInternalSessionMeta(const netlib::ISessionPtr& spSession)
{
    return static_cast<InternalSessionMeta*>(spSession->GetUserData().get());
}
