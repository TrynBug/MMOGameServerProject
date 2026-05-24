using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace MMO.Client.Navigation
{
    /// <summary>
    /// NavMesh 의 생명주기를 관리하는 정적 서비스.
    ///
    /// 책임:
    /// - 스테이지 이름으로 .bin 파일을 로드 (StreamingAssets/NavMesh/&lt;stage&gt;.bin)
    /// - "현재 어떤 스테이지가 로드되어 있나" 추적
    /// - 스테이지 전환 시 이전 것 해제 후 새로 로드 (스테이지당 1개 NavMesh 정책)
    /// - 게임 코드에서 호출하는 FindPath / SamplePosition 의 entry point
    ///
    /// 정적 클래스인 이유:
    /// - NavMesh 는 전역 게임 상태에 가까움 (한 시점에 1개 스테이지)
    /// - 의존성 주입 시스템이 아직 없는 단계라 가장 단순한 형태로 시작
    /// - 추후 매니저 시스템이 잡히면 그쪽으로 옮기기 쉬움
    /// </summary>
    public static class NavMeshService
    {
        // 현재 로드된 NavMesh. 한 시점에 0개 또는 1개.
        private static NavMeshRuntime sm_current;

        /// <summary>현재 로드된 스테이지 이름 (없으면 null)</summary>
        public static string CurrentStageName => sm_current?.StageName;

        /// <summary>NavMesh 가 사용 가능한 상태인지</summary>
        public static bool IsLoaded => sm_current != null;

        /// <summary>현재 NavMeshRuntime 인스턴스 (디버그/고급 용도, 일반적으로는 사용하지 않음)</summary>
        public static NavMeshRuntime Current => sm_current;

        // ---------------------------------------------------------------------
        // 로드 / 언로드
        // ---------------------------------------------------------------------

        /// <summary>
        /// 스테이지 이름으로 NavMesh 를 로드한다.
        ///
        /// 동작:
        /// - 이미 같은 스테이지가 로드되어 있으면 아무 일도 하지 않음 (true 반환).
        /// - 다른 스테이지가 로드되어 있으면 기존 것 언로드 후 새로 로드.
        /// - 파일 경로는 StreamingAssets/NavMesh/&lt;stageName&gt;.bin 으로 고정.
        /// </summary>
        /// <returns>로드 성공 시 true</returns>
        public static bool Load(string stageName)
        {
            if (string.IsNullOrEmpty(stageName))
            {
                Debug.LogError("[NavMeshService] stageName 이 비어있습니다.");
                return false;
            }

            // 이미 로드된 같은 스테이지면 no-op
            if (sm_current != null && sm_current.StageName == stageName)
                return true;

            // 다른 스테이지가 있으면 해제
            if (sm_current != null)
                Unload();

            string path = GetBinPath(stageName);
            var rt = NavMeshRuntime.Load(path, stageName);
            if (rt == null)
                return false;

            sm_current = rt;
            Debug.Log($"[NavMeshService] 로드 성공: {stageName} ({path})");
            return true;
        }

        /// <summary>현재 로드된 NavMesh 를 해제한다.</summary>
        public static void Unload()
        {
            if (sm_current == null)
                return;

            string stage = sm_current.StageName;
            sm_current = null;
            // DtNavMesh 는 GC 가 알아서 정리. C++ 처럼 명시적 dispose 불필요.
            Debug.Log($"[NavMeshService] 언로드: {stage}");
        }

        // ---------------------------------------------------------------------
        // 게임 코드용 API (NavMeshRuntime 으로 위임)
        // ---------------------------------------------------------------------

        /// <summary>
        /// 시작점에서 끝점까지의 경로를 outPath 에 채운다.
        /// NavMesh 가 로드되지 않았거나 경로를 못 찾으면 false 반환.
        /// </summary>
        public static bool FindPath(Vector3 start, Vector3 end, List<Vector3> outPath)
        {
            if (sm_current == null)
            {
                if (outPath != null) outPath.Clear();
                return false;
            }
            return sm_current.FindPath(start, end, outPath);
        }

        /// <summary>
        /// 임의 위치를 NavMesh 위로 보정한다.
        /// NavMesh 가 로드되지 않았으면 false 반환 (result = input).
        /// </summary>
        public static bool SamplePosition(Vector3 input, float searchRadius, out Vector3 result)
        {
            if (sm_current == null)
            {
                result = input;
                return false;
            }
            return sm_current.SamplePosition(input, searchRadius, out result);
        }

        // ---------------------------------------------------------------------
        // 경로 규칙
        // ---------------------------------------------------------------------

        /// <summary>
        /// 스테이지 이름으로 .bin 파일 경로를 만든다.
        /// 빌드 도구 (NavMeshBuilder) 가 저장하는 경로와 일치해야 한다.
        /// </summary>
        public static string GetBinPath(string stageName)
        {
            // StreamingAssets/NavMesh/&lt;stage&gt;.bin
            // 빌드된 게임에서도 StreamingAssets 는 그대로 파일로 접근 가능 (Windows/PC 기준).
            return Path.Combine(Application.streamingAssetsPath, "NavMesh", stageName + ".bin");
        }
    }
}
