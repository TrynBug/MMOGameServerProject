#include "pch.h"
#include "Crypto.h"

// ====================================================================
// ChaCha20 구현 (STUB)
//
// 실제 구현 방법 (추천: libsodium)
// ------------------------------------------------------------------
// 1) vcpkg 로 libsodium 설치
//      vcpkg install libsodium:x64-windows-static
//    또는 NuGet 의 libsodium 패키지 사용.
//
// 2) pch.h 또는 이 파일에 다음 추가:
//      #include <sodium.h>
//      #pragma comment(lib, "libsodium.lib")
//
// 3) 프로세스 시작 시 1회 sodium_init() 호출 필요 (NetServer::Initialize 최상단).
//
// 4) 아래 EncryptInPlace를 다음과 같이 구현:
//
//      crypto_stream_chacha20_ietf_xor_ic(
//          data, data, static_cast<unsigned long long>(size),
//          nonce, counter, key);
//
//    ChaCha20(IETF, RFC 8439)은 스트림 암호이므로 동일 key/nonce/counter로
//    XOR 두 번 적용 시 원문이 복원됩니다. 그래서 Encrypt == Decrypt.
//
// 주의
// ------------------------------------------------------------------
// - key, nonce는 세션별로 안전하게 교환되어야 합니다 (핸드셰이크).
//   이 라이브러리는 키 교환 프로토콜을 제공하지 않습니다. 상위 레이어(게임서버)가
//   X25519, ECDH 등을 이용해 세션키를 생성/교환해 주어야 합니다.
// - 같은 key/nonce 쌍을 서로 다른 메시지에 재사용하면 보안이 무너집니다.
//   nonce를 패킷 카운터와 연결하거나 메시지별로 증가시키세요.
// - 인증(MAC)이 필요하면 ChaCha20-Poly1305 (AEAD)를 사용하세요.
//   libsodium의 crypto_aead_chacha20poly1305_ietf_encrypt 계열 API 참고.
// ====================================================================

namespace netlib
{

void ChaCha20::EncryptInPlace(uint8* data, int32 size,
                               const uint8 key[KEY_SIZE],
                               const uint8 nonce[NONCE_SIZE],
                               uint32 counter)
{
    (void)data;
    (void)size;
    (void)key;
    (void)nonce;
    (void)counter;

    // TODO: libsodium 연동 후 아래 코드로 교체
    //
    //   crypto_stream_chacha20_ietf_xor_ic(
    //       data, data, static_cast<unsigned long long>(size),
    //       nonce, counter, key);

    // 현재는 no-op. 라이브러리 빌드만 통과시키기 위한 stub.
}

} // namespace netlib
