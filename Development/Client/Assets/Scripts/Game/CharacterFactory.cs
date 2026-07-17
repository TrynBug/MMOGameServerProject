using Client.Managers;
using UnityEngine;

namespace Client.Game
{
    // PlayerCharacter GameObject 를 생성하는 정적 팩토리.
    //
    // 책임:
    //   - Resources 에서 캐릭터 prefab 로드 + Instantiate
    //   - PlayerCharacter 컴포넌트 초기화 (Initialize 호출)
    //   - LocalPlayer 인 경우 PlayerMoveController 부착
    //
    // 책임이 아닌 것:
    //   - 캐릭터 컬렉션 관리 (StageManager 가 함)
    //   - 캐릭터 선택 흐름 (CharacterSelector 가 함)
    //
    // 향후 직업별 prefab, 머터리얼 교체, 무기 장착 등이 들어오면 이 함수가 커질 예정.
    // 그 시점에 MonoBehaviour 매니저로 승격할 수 있음.
    //
    // prefab 경로:
    //   직업(EJob) + 외형 프리셋 인덱스로 "Prefabs/Characters/{Job}_{preset}" 분기.
    //   예) Mage_0, Mage_1, Warrior_0, Warrior_1.
    public static class CharacterFactory
    {
        public readonly struct PrefabProfile
        {
            public int JobId { get; }
            public int PresetId { get; }
            public string PrefabPath { get; }

            public PrefabProfile(int jobId, int presetId, string prefabPath)
            {
                JobId = jobId;
                PresetId = presetId;
                PrefabPath = prefabPath;
            }
        }

        // 폴백 prefab (잘못된 job/preset 또는 로드 실패 시).
        private const string k_fallbackPrefabPath = "Prefabs/Characters/Mage_0";

        // 직업당 프리셋 개수 (Stage 1 에서 직업당 2종 제작).
        private const int k_presetCount = 2;

        // (jobId, presetId) → Resources prefab 경로. job/preset 매핑은 여기 한 곳에 둔다.
        // (CharacterPreviewRig 등에서도 재사용)
        public static string ResolvePrefabPath(int jobId, int presetId)
        {
            // EJob: Mage=1, Warrior=2 (GameEnum_Common). 그 외는 폴백.
            string job = jobId switch
            {
                (int)GameData.EJob.Mage => "Mage",
                (int)GameData.EJob.Warrior => "Warrior",
                _ => null,
            };
            if (job == null) return k_fallbackPrefabPath;

            int preset = Mathf.Clamp(presetId, 0, k_presetCount - 1);
            return $"Prefabs/Characters/{job}_{preset}";
        }

        /// <summary>에디터 export가 모든 플레이어 외형 프리팹을 순회할 때 사용하는 목록입니다.</summary>
        public static System.Collections.Generic.IEnumerable<PrefabProfile> GetSupportedPrefabProfiles()
        {
            int[] jobs = { (int)GameData.EJob.Mage, (int)GameData.EJob.Warrior };
            foreach (int jobId in jobs)
            for (int presetId = 0; presetId < k_presetCount; ++presetId)
                yield return new PrefabProfile(jobId, presetId, ResolvePrefabPath(jobId, presetId));
        }

        // jobId/presetId 를 받아 해당 직업/프리셋 prefab 으로 생성한다.
        // jobId 미지정(0 등)이면 폴백 prefab 사용 (원격 캐릭터 등 직업 정보가 아직 없을 때).
        public static PlayerCharacter Create(long objectId, string name, bool isLocalPlayer, Vector3 pos, float dirY, int jobId = 0, int presetId = 0)
        {
            // prefab 로드 + Instantiate.
            // 현재는 동기 Load (Resources). 추후 Addressables 도입 시 InstantiateAsync 로 교체.
            string prefabPath = ResolvePrefabPath(jobId, presetId);
            GameObject go = Managers.Managers.Resource.Instantiate(prefabPath);
            if (go == null && prefabPath != k_fallbackPrefabPath)
            {
                Debug.LogWarning($"[CharacterFactory] prefab 로드 실패: {prefabPath}. 폴백 사용.");
                go = Managers.Managers.Resource.Instantiate(k_fallbackPrefabPath);
            }
            if (go == null)
            {
                Debug.LogError($"[CharacterFactory] PlayerCharacter prefab 로드 실패: {prefabPath}");
                return null;
            }

            // prefab 에 부착된 PlayerCharacter 컴포넌트 획득.
            // (prefab 에 미리 붙어 있어야 함)
            PlayerCharacter pc = go.GetComponent<PlayerCharacter>();
            if (pc == null)
            {
                Debug.LogError($"[CharacterFactory] prefab 에 PlayerCharacter 컴포넌트가 없습니다: {prefabPath}");
                GameObject.Destroy(go);
                return null;
            }

            pc.Initialize(objectId, name, isLocalPlayer, pos, dirY);

            // 머리 위 체력바 부착 (코드 생성 placeholder). 로컬 플레이어를 빼려면 if (!isLocalPlayer) 가드.
            HealthBar.Attach(pc);

            // LocalPlayer 에만 입력→이동 컨트롤러 부착.
            // 타 유저 prefab 에는 PlayerMoveController 가 없어야 함 (구조적 보장).
            if (isLocalPlayer)
            {
                go.AddComponent<PlayerMoveController>();
            }

            Debug.Log($"[CharacterFactory] Created {go.name} at {pos}");
            return pc;
        }
    }
}
