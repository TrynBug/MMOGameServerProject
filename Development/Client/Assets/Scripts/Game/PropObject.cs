using UnityEngine;

namespace Client.Game
{
    // 1개 prop(문/레버/스위치/포탈 등)을 표현하는 컴포넌트.
    // 서버의 ObjectVisibilityNtf(prop_spawns) 수신 시 StageManager 가 PropFactory 로 동적 생성한다.
    //
    // ── 몬스터와의 차이 ──
    //   - 정적이다: 위치 SnapshotNtf 를 받지 않는다(스폰 시 위치/회전 고정).
    //   - HP/스탯/버프 없음: ActorObject 가 아니라 MonoBehaviour 를 직접 상속한다.
    //   - 상태(state) 보유: 서버 권위 상태값. PropStateNtf 수신 시 SetState 로 갱신하고
    //     비주얼(문 열림/레버 On 등)로 전환한다.
    //
    // ── 상태 → 비주얼 ──
    // prop prefab 은 임의의 아트 에셋이므로 상태별 비주얼은 prefab(Animator) 책임이다.
    // 이 컴포넌트는 일반 메커니즘으로 Animator 의 정수 파라미터 "State" 에 상태값을 밀어넣는다.
    // (Animator 컨트롤러가 State 값 → 애니메이션/포즈를 매핑.) 파라미터가 없으면 비주얼 변화 없이 값만 보관한다.
    public class PropObject : MonoBehaviour
    {
        // 서버가 발급한 오브젝트 식별자 (디스폰/타게팅 시 이 값으로 찾는다).
        public long ObjectId { get; private set; }

        // prop 종류 데이터 Key (GameData_Prop). 프리팹 조회에 사용.
        public int PropKey { get; private set; }

        // 현재 상태값 (서버 권위). PropStateNtf 로 갱신.
        public int State { get; private set; }

        // 상태 → 비주얼 전환용 Animator(없을 수 있음, 그 경우 값만 보관).
        private Animator m_animator;
        private bool m_hasStateParam;
        private static readonly int s_stateHash = Animator.StringToHash("State");

        // PropFactory 가 생성 직후 1회 호출.
        public void Initialize(long objectId, int propKey, Vector3 pos, float dirY, int state)
        {
            ObjectId = objectId;
            PropKey = propKey;
            State = state;

            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);

            // Animator "State" 정수 파라미터 존재 여부를 캐시(상태 전환마다 재탐색 회피).
            m_animator = GetComponentInChildren<Animator>();
            m_hasStateParam = false;
            if (m_animator != null)
            {
                foreach (AnimatorControllerParameter p in m_animator.parameters)
                {
                    if (p.nameHash == s_stateHash && p.type == AnimatorControllerParameterType.Int)
                    {
                        m_hasStateParam = true;
                        break;
                    }
                }
            }

            applyStateVisual();
        }

        // 서버 PropStateNtf 수신 시 호출(StageManager). 상태값 갱신 + 비주얼 전환.
        public void SetState(int state)
        {
            if (State == state)
                return;
            State = state;
            applyStateVisual();
        }

        // 현재 상태값을 Animator 파라미터로 반영. 파라미터가 없으면 no-op.
        private void applyStateVisual()
        {
            if (m_hasStateParam)
                m_animator.SetInteger(s_stateHash, State);
        }
    }
}
