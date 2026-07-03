using GameData;
using UnityEngine;
using Client.Network;

namespace Client.Game
{
    // 1마리 몬스터를 표현하는 컴포넌트.
    // 서버의 ObjectVisibilityNtf(monster_spawns) 수신 시 StageManager 가 동적으로 생성한다.
    //
    // ── 이동 (서버 권위, 원격 재생) ──
    // 몬스터는 항상 원격이므로 navmesh 재현 없이 IRemoteMotionDriver 로 재생한다(PlayerCharacter 의 원격 처리와 동일).
    // 현재 구현체는 SnapshotNtf 스트림을 보간하는 SnapshotInterpolator. 재생 루프는 드라이버 종류에 무관하다.
    //
    // 애니메이션은 IActorAnimator 를 통해 idle/move 를 재생한다(스냅샷의 이동 플래그 기반).
    // HP/스탯/버프는 ActorObject 에서 상속한다. 현재 HP 는 SkillDamageNtf 의 remaining_hp 로 갱신되고,
    // 최대 HP 는 스폰 시 게임데이터로 시드한다(서버가 몬스터 스탯 패킷을 보내기 전까지의 v1 처리).
    // prefab 은 임의의 아트 에셋이라 이 컴포넌트가 미리 붙어있지 않을 수 있어 MonsterFactory 가 런타임에 AddComponent 한다.
    public class MonsterObject : ActorObject
    {
        // 서버가 발급한 오브젝트 식별자 (디스폰 시 이 값으로 찾는다).
        public long ObjectId { get; private set; }

        // 몬스터 게임데이터 Key (종류 식별).
        public int MonsterKey { get; private set; }

        // 몬스터 등급 (노말~보스). 오토타게팅의 "최고 등급" 모드가 읽는다. 스폰 시 게임데이터에서 채운다.
        public EMonsterGrade Grade { get; private set; } = EMonsterGrade.Normal;

        // ── 원격 모션 드라이버 ──────────────────────────────────────
        // 몬스터는 항상 원격이다. 재생은 IRemoteMotionDriver 너머로만 한다(updateMotion).
        // 현재 구현체는 스냅샷 보간(SnapshotInterpolator). 재생 루프는 드라이버 종류에 무관하다.
        private readonly ISnapshotMotionDriver m_motion = new SnapshotInterpolator();
        private bool m_remoteMoving;

        // 공통 애니메이터. idle/move 등 의미론적 상태를 Animator 파라미터로 변환한다.
        // 없을 수 있음(아트 미적용 prefab) → 그 경우 애니메이션 없이 동작.
        private IActorAnimator m_actorAnimator;

        // MonsterFactory 가 생성 직후 1회 호출.
        public void Initialize(long objectId, int monsterKey, Vector3 pos, float dirY, bool isDead, double curHp, double maxHp)
        {
            ObjectId = objectId;
            MonsterKey = monsterKey;

            // 등급(Grade)은 게임데이터에서 채운다 (서버가 등급은 안 보냄).
            seedFromGameData(monsterKey);

            // HP 는 서버 권위값으로 설정한다. 최대치(HpTotal)를 먼저 넣어야 SetCurHp 의 clamp 가 올바르다.
            Stats.Set(EStat.HpTotal, maxHp);
            SetCurHp(curHp);

            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);

            // 공통 애니메이터 해석. AddComponent 타이밍 의존을 피하려고 Awake 가 아니라
            // (모든 컴포넌트가 부착된 뒤 호출되는) Initialize 에서 찾는다. 없으면 애니메이션 없이 동작.
            m_actorAnimator = GetComponentInChildren<IActorAnimator>();

            // corpse 유지 중 늦게 진입한 경우(is_dead): 사망 끝 포즈로 고정(애니메이션 재생 없음).
            if (isDead)
                SpawnAsCorpse();
        }

        // 게임데이터에서 등급(Grade)을 읽어 채운다. HP(현재/최대)는 서버 권위값을 Initialize 에서 설정한다.
        private void seedFromGameData(int monsterKey)
        {
            GameData_Monster data = GameDataTable_Monster.FindData(monsterKey);
            if (data == null)
                return;

            Grade = data.Grade;
        }

        // ─── 서버 스냅샷 처리 ─────────────────────────────────────────────

        // 서버 SnapshotNtf 수신 시 호출(StageManager). 모션 드라이버에 스냅샷을 먹인다.
        public void OnSnapshot(double serverTimeMs, Vector3 pos, float yaw, bool moving)
        {
            m_motion.OnSnapshot(serverTimeMs, pos, yaw, moving);
        }

        private void Update()
        {
            updateMotion();
            updateAnimator();
        }

        // 1프레임 재생. 드라이버가 자체 타임라인에서 뽑은 위치/회전을 적용한다(클럭 선택은 드라이버 소유).
        private void updateMotion()
        {
            if (m_motion.Sample(out Vector3 pos, out float yaw, out bool moving))
            {
                transform.position = pos;
                transform.rotation = Quaternion.Euler(0f, yaw, 0f);
                m_remoteMoving = moving;
            }
        }

        // 이동 상태(스냅샷 플래그)를 애니메이터에 반영. 매 프레임 그대로 밀어넣는다.
        private void updateAnimator()
        {
            if (m_actorAnimator == null)
                return;

            m_actorAnimator.SetMoving(m_remoteMoving);
        }

        // ─── 공격 시전 ────────────────────────────────────────────────────

        // 서버 AbilityCastNtf 수신 시 호출(SkillSystem). 시전 방향으로 즉시 회전 + 윈드업 모션 재생.
        // 텔레그래프(바닥 예고)는 SkillSystem 이 별도로 spawn 한다(월드 고정 오브젝트라 몬스터에 붙이지 않음).
        public void PlayAbilityCast(Vector3 dir)
        {
            if (dir.sqrMagnitude > 0.0001f)
                transform.rotation = Quaternion.LookRotation(new Vector3(dir.x, 0f, dir.z));

            m_actorAnimator?.PlaySkill();
        }

        // 피격 반응 애니메이션. 드라이버(AnimatorActorAnimator)가 'Locomotion 중 + 쓰로틀' 게이트를 적용한다.
        public void PlayHitReaction()
        {
            m_actorAnimator?.PlayHit();
        }

        // ─── 사망 처리 ────────────────────────────────────────────────────

        // 사망 처리: 사망 애니메이션. 이미 사망 상태면 멱등.
        public override void OnDeath()
        {
            enterDeadState(playAnimation: true);   // 막 죽음: 사망 애니메이션을 처음부터 재생.
        }

        // corpse 상태로 늦게 진입한 경우: 사망 상태로 들어가되 애니메이션 재생 없이 끝 포즈로 고정.
        public void SpawnAsCorpse()
        {
            enterDeadState(playAnimation: false);
        }

        // 사망 상태 진입 공통 처리. 이미 사망이면 멱등.
        // 이동은 서버 권위라 사망 시 서버가 위치를 멈추고 스냅샷이 그대로 유지되므로 클라가 따로 멈출 필요 없다.
        private void enterDeadState(bool playAnimation)
        {
            if (IsDead)
                return;
            base.OnDeath();   // IsDead = true (ActorObject)
            if (playAnimation)
                m_actorAnimator?.PlayDead();
            else
                m_actorAnimator?.SetDeadPose();
        }
    }
}
