#pragma once

#include "Types.h"

namespace netlib
{

// ====================================================================
// Crypto
// 네트워크라이브러리.md "패킷 암호화" 섹션: ChaCha20 알고리즘 사용.
//
// 이 헤더/구현은 인터페이스 stub 입니다. 실제 암호화 루틴은 libsodium 등
// 외부 라이브러리 연동 후 채워 넣으세요.
//
// 사용 시나리오
//   1. 핸드셰이크에서 Session 별로 Key, Nonce 교환 (상위 레이어가 담당)
//   2. Send 전에 PacketFlags::Encrypted 가 세팅된 패킷의 payload에 대해 ChaCha20::Encrypt
//   3. Recv 후 PacketFlags::Encrypted 가 세팅되어 있으면 ChaCha20::Decrypt
//
// 편의상 ChaCha20은 카운터 모드이므로 Encrypt/Decrypt가 동일 연산입니다.
// ====================================================================
class ChaCha20
{
public:
    static constexpr int32 KEY_SIZE   = 32;   // 256-bit
    static constexpr int32 NONCE_SIZE = 12;   // 96-bit (IETF RFC 8439)

    // 지정한 key, nonce, counter로 input을 암/복호화 하여 output에 쓴다.
    // input/output은 같은 포인터여도 됨 (in-place).
    // TODO: libsodium crypto_stream_chacha20_ietf_xor 로 구현.
    static void EncryptInPlace(uint8* data, int32 size,
                                const uint8 key[KEY_SIZE],
                                const uint8 nonce[NONCE_SIZE],
                                uint32 counter = 0);

    static void DecryptInPlace(uint8* data, int32 size,
                                const uint8 key[KEY_SIZE],
                                const uint8 nonce[NONCE_SIZE],
                                uint32 counter = 0)
    {
        // ChaCha20 스트림 암호이므로 Encrypt == Decrypt.
        EncryptInPlace(data, size, key, nonce, counter);
    }
};

} // namespace netlib
