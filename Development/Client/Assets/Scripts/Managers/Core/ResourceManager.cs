using System;
using System.Threading.Tasks;
using UnityEngine;

namespace Client.Managers
{
    // 프리팹/에셋 로드를 담당하는 매니저.
    //
    // 두 가지 API 를 제공한다:
    //   - 동기 (Resources.Load 기반)
    //     UI 프리팹, 효과음 등 가볍고 즉시 떠야 하는 자산에 사용.
    //     자산 경로는 Assets/Resources/ 이하 상대경로.
    //
    //   - 비동기 (Addressables 기반, 아직 미구현)
    //     캐릭터/몬스터 모델, 이펙트, BGM 등 무거운 자산용.
    //     시그니처만 정의되어 있고 호출 시 NotImplementedException.
    //     실제 구현은 Addressables 패키지 설치 + 그룹 설정 후 추가.
    //
    // 어떤 자산이 동기 / 비동기인지 규칙:
    //   동기  - UI 프리팹, 효과음 (작고 즉시 사용)
    //   비동기 - 캐릭터/몬스터/이펙트/머터리얼/BGM (크고 미리 로드 가능)
    //
    // 풀링은 현재 미구현. PoolManager 도입 시 Instantiate/Destroy 안에서 통합 예정.
    public class ResourceManager
    {
        // ─── 동기 API (Resources) ─────────────────────────────────────

        // 임의 타입 자산 로드. 실패 시 null + 에러 로그.
        // path: Resources/ 하위 경로 (확장자 없이). 예) "UI/Popup/UI_Confirm"
        public T Load<T>(string path) where T : UnityEngine.Object
        {
            T asset = Resources.Load<T>(path);
            if (asset == null)
                Debug.LogError($"[ResourceManager] Failed to load asset: {path}");
            return asset;
        }

        // GameObject 프리팹을 로드해서 즉시 Instantiate.
        // "(Clone)" 접미사는 제거.
        public GameObject Instantiate(string path, Transform parent = null)
        {
            GameObject prefab = Load<GameObject>(path);
            if (prefab == null) return null;

            GameObject go = UnityEngine.Object.Instantiate(prefab, parent);
            go.name = prefab.name;
            return go;
        }

        // GameObject 파괴. 풀링 도입 시 여기에 풀 반환 로직 추가.
        public void Destroy(GameObject go)
        {
            if (go == null) return;
            UnityEngine.Object.Destroy(go);
        }

        // ─── 비동기 API (Addressables) — 시그니처만, 미구현 ────────────
        //
        // 호출 시점: 캐릭터/몬스터/이펙트 등 무거운 자산이 필요해질 때 구현.
        // 구현 시 필요한 작업:
        //   1. Package Manager 에서 Addressables 패키지 설치
        //   2. 각 자산에 Addressable 체크 + 키 부여 (인스펙터)
        //   3. 아래 메서드 내부를 Addressables.LoadAssetAsync / InstantiateAsync 로 채움
        //   4. 핸들 추적 / Release 로직 추가 (메모리 누수 방지)
        //
        // 호출 시그니처는 미리 결정되어 있으므로 호출자는 안심하고 사용 가능.
        // (구현되기 전까지는 NotImplementedException — 컴파일은 되지만 런타임에서 막힘)

        public Task<T> LoadAsync<T>(string key) where T : UnityEngine.Object
        {
            throw new NotImplementedException(
                "LoadAsync 는 Addressables 도입 후 구현됩니다. " +
                "지금은 동기 Load<T> 를 사용하세요.");
        }

        public Task<GameObject> InstantiateAsync(string key, Transform parent = null)
        {
            throw new NotImplementedException(
                "InstantiateAsync 는 Addressables 도입 후 구현됩니다. " +
                "지금은 동기 Instantiate 를 사용하세요.");
        }
    }
}
