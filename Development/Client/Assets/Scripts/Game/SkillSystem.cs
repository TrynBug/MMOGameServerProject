using System.Collections;
using System.Collections.Generic;
using Client.Managers;
using Client.Network;
using Client.Packet;
using Common;
using GameData;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // 스킬 시스템 허브. StageManager 처럼 Game 씬에 배치되는 컴포넌트.
    //
    // 책임:
    //   - SkillCastNtf / SkillDamageNtf 수신 처리
    //   - 로컬 시전(OnSkill1): 오토타게팅 → SkillCastReq 송신 → 예측 투사체 발사
    //   - 투사체 그룹 보관, effect_id 바인딩, hit 배치 플러시(SkillProjectileHitReq)
    //   - DamageNtf 로 대상 HP 갱신 + 데미지 숫자 표시
    //
    // 책임이 아닌 것:
    //   - 월드 오브젝트 생성/관리 (StageManager 가 함)
    //   - 저수준 입력 (InputManager 가 함)
    //
    // 마일스톤 1: 파이어볼 수직 슬라이스만. (쿨타임/마나 게이팅은 클라 선체크 수준, 서버 검증은 5c.)
    public class SkillSystem : MonoBehaviour
    {
        public static SkillSystem Instance { get; private set; }

        // 마일스톤 1: OnSkill1 = 파이어볼. (스킬 슬롯 매핑은 추후 스킬창/장비에서.)
        [SerializeField] private long m_skill1Key = 1001;

        // 투사체 비주얼의 Y 오프셋 (지면에 반쯤 묻히지 않도록). XZ 판정에는 영향 없음.
        [SerializeField] private float m_projectileHeight = 0.5f;

        // 시전 클립의 기본 길이(초). CastSpeed = 이 값 / ActionLock초 로 시전 애니를 ActionLock 에 맞춘다.
        // v1 은 모든 스킬이 같은 시전 클립을 공유한다고 보고 하나로 둔다.
        // 스킬마다 시전 클립이 달라지면 Skill 데이터에 client 전용 CastAnimLength 컴럼으로 옮긴다.
        [SerializeField] private float m_castClipBaseLength = 0.8f;

        private readonly ISkillTargetingMode m_targeting = new NearestTargeting();

        // 시전 클라가 발사한 투사체 그룹들 (effect_id 바인딩 + hit 보고 대상).
        private readonly List<SkillProjectileGroup> m_groups = new List<SkillProjectileGroup>();

        // 다음 시전 가능 시각 (액션락). Time.time 기준.
        private float m_actionLockUntil;

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(this);
                return;
            }
            Instance = this;

            PacketDispatcher.Instance.Register<SkillCastNtf>(GamePacketId.SkillCastNtf, onSkillCastNtf);
            PacketDispatcher.Instance.Register<SkillDamageNtf>(GamePacketId.SkillDamageNtf, onSkillDamageNtf);

            Debug.Log("[SkillSystem] Ready.");
        }

        private void OnEnable()
        {
            InputManager input = Managers.Managers.Input;
            if (input != null)
                input.OnSkill1 += castSkill1;
        }

        private void OnDisable()
        {
            InputManager input = Managers.Managers.Input;
            if (input != null)
                input.OnSkill1 -= castSkill1;
        }

        private void OnDestroy()
        {
            if (Instance == this)
                Instance = null;
        }

        // ─── 시전 ────────────────────────────────────────────────────
        private void castSkill1() => tryCast(m_skill1Key);

        private void tryCast(long skillKey)
        {
            if (Time.time < m_actionLockUntil)
                return;   // 이전 시전 애니메이션(액션락) 중.

            GameData_Skill skill = GameDataTable_Skill.FindData(skillKey);
            if (skill == null)
            {
                Debug.LogWarning($"[SkillSystem] skill data 없음: {skillKey}");
                return;
            }

            PlayerCharacter caster = StageManager.Instance?.LocalPlayer;
            if (caster == null)
                return;

            // 마나 선체크 (클라가 먼저 막음. 서버도 검증 — 5c).
            if (caster.CurMp < skill.ManaCost)
                return;

            // 방향/타겟 결정 (오토타게팅: 가장 가까운 적, 없으면 캐릭터 정면).
            Vector3 origin = caster.transform.position;
            Vector3 dir;
            long targetId = 0;
            Vector3 targetPos = origin;

            MonsterObject target = m_targeting.SelectTarget(origin, (float)skill.MaxRange);
            if (target != null)
            {
                Vector3 to = target.transform.position - origin;
                to.y = 0f;
                dir = (to.sqrMagnitude > 0.0001f) ? to.normalized : flatForward(caster);
                targetId = target.ObjectId;
                targetPos = target.transform.position;
            }
            else
            {
                dir = flatForward(caster);
            }

            // 캐릭터 즉시 회전 (Rotation=true 인 스킬).
            if (skill.Rotation)
                caster.transform.rotation = Quaternion.LookRotation(dir);

            // 시전 애니메이션 재생 (CastAnim 이 있으면).
            float actionLockSec = skill.ActionLockMs / 1000f;
            float castSpeed = (m_castClipBaseLength > 0f && actionLockSec > 0.01f)
                ? m_castClipBaseLength / actionLockSec
                : 1f;

            if (skill.CastClass == ESkillCastClass.Mobile)
            {
                // 이동시전: 상반신만 시전, 다리는 locomotion 유지 (이동 잠금 없음).
                caster.PlayCastUpperBody(castSpeed, actionLockSec);
            }
            else
            {
                // 그 외(Stationary 등): 전신 시전. Stationary 는 시전 동안 이동 잠금.
                caster.PlayCastAnimation(skill.CastAnim, castSpeed);
                if (skill.CastClass == ESkillCastClass.Stationary)
                    caster.LockMovement(actionLockSec);
            }

            // 액션락 설정 (다음 시전까지 잠금).
            m_actionLockUntil = Time.time + skill.ActionLockMs / 1000f;

            // 서버에 시전 요청 즉시 송신 (서버는 자기 CastDelay 후 entry 발동).
            sendCastReq(skill, origin, dir, targetId, targetPos);

            // 투사체 스킬이면 예측 발사 (CastDelay 후). 그룹은 즉시 등록(effect_id 바인딩 대기).
            if (skill.EffectDamage == ESkillEffectDamage.ContactHit)
            {
                SkillProjectileGroup group = new SkillProjectileGroup(skillKey);
                m_groups.Add(group);
                StartCoroutine(spawnProjectilesAfterDelay(group, skill, origin, dir));
            }
        }

        // 캐릭터 정면(수평). 정면이 거의 수직이면 월드 forward 폴백.
        private static Vector3 flatForward(PlayerCharacter caster)
        {
            Vector3 fwd = caster.transform.forward;
            fwd.y = 0f;
            return (fwd.sqrMagnitude > 0.0001f) ? fwd.normalized : Vector3.forward;
        }

        private IEnumerator spawnProjectilesAfterDelay(SkillProjectileGroup group, GameData_Skill skill, Vector3 origin, Vector3 dir)
        {
            if (skill.CastDelayMs > 0)
                yield return new WaitForSeconds(skill.CastDelayMs / 1000f);

            spawnFan(group, skill, origin, dir);
        }

        private void sendCastReq(GameData_Skill skill, Vector3 origin, Vector3 dir, long targetId, Vector3 targetPos)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
                return;

            SkillCastReq req = new SkillCastReq
            {
                SkillKey = skill.Key,
                OriginX = origin.x,
                OriginY = origin.y,
                OriginZ = origin.z,
                DirX = dir.x,
                DirZ = dir.z,
                Seed = 0,                 // 마일스톤 1: scatter 미사용 → seed 무관.
                TargetObjectId = targetId,
                TargetPosX = targetPos.x,
                TargetPosZ = targetPos.z,
            };
            net.Send(GamePacketId.SkillCastReq, req);
        }

        // ─── CastNtf ─────────────────────────────────────────────────
        private void onSkillCastNtf(SkillCastNtf ntf)
        {
            long localId = StageManager.Instance != null && StageManager.Instance.LocalPlayer != null
                ? StageManager.Instance.LocalPlayer.UserId
                : 0;

            if (ntf.CasterObjectId == localId)
            {
                // 내 시전: 돌아온 effect_id 를 예측 발사한 그룹(가장 오래된 미바인딩 동일 스킬)에 바인딩.
                // (이미 예측 발사했으므로 여기서 다시 스폰하지 않는다.)
                bindEffectId(ntf.SkillKey, ntf.EffectId);
                return;
            }

            // 다른 캐스터: 비주얼 전용 투사체 재현 (보고 안 함).
            spawnRemoteVisual(ntf);
        }

        private void bindEffectId(long skillKey, long effectId)
        {
            if (effectId == 0)
                return;

            foreach (SkillProjectileGroup g in m_groups)
            {
                if (!g.HasEffectId && g.SkillKey == skillKey)
                {
                    g.AssignEffectId(effectId);
                    return;
                }
            }
        }

        private void spawnRemoteVisual(SkillCastNtf ntf)
        {
            GameData_Skill skill = GameDataTable_Skill.FindData(ntf.SkillKey);
            if (skill == null || skill.EffectDamage != ESkillEffectDamage.ContactHit)
                return;

            Vector3 origin = new Vector3(ntf.OriginX, ntf.OriginY, ntf.OriginZ);
            Vector3 dir = new Vector3(ntf.DirX, 0f, ntf.DirZ);
            dir = (dir.sqrMagnitude > 0.0001f) ? dir.normalized : Vector3.forward;

            spawnFan(group: null, skill, origin, dir);   // group=null → 비주얼 전용.
        }

        // ─── 투사체 발사 (로컬/원격 공용) ────────────────────────────
        private void spawnFan(SkillProjectileGroup group, GameData_Skill skill, Vector3 origin, Vector3 dir)
        {
            // 투사체 prefab 을 1회 로드 (실패 시 ResourceManager 가 에러 로그 1회). 폴백 없음 — 데이터가 맞아야 동작.
            GameObject prefab = Managers.Managers.Resource.Load<GameObject>(skill.ProjectilePrefabPath);
            if (prefab == null)
            {
                group?.MarkLaunched(0);   // 그룹이 정리될 수 있도록 0발로 마감.
                return;
            }

            int count = Mathf.Max(1, (int)skill.ProjectileCount);
            List<Vector3> dirs = computeFanDirs(dir, count, (float)skill.FanAngleDeg);

            Vector3 startPos = origin + Vector3.up * m_projectileHeight;   // 비주얼 높이 (XZ 판정엔 영향 없음).
            for (int i = 0; i < dirs.Count; ++i)
                spawnOneProjectile(prefab, group, i, startPos, dirs[i], (float)skill.ProjectileSpeed, (float)skill.MaxRange);

            group?.MarkLaunched(dirs.Count);
        }

        private void spawnOneProjectile(GameObject prefab, SkillProjectileGroup group, int index, Vector3 startPos, Vector3 dir, float speed, float maxRange)
        {
            GameObject go = Instantiate(prefab);
            go.name = "Projectile";

            // OnTriggerEnter 발생 조건: 트리거 콜라이더 + (kinematic) Rigidbody.
            // prefab 은 VFX 용이라 둘 다 없을 수 있으므로 필요한 컴포넌트를 보장한다.
            Rigidbody rb = go.GetComponent<Rigidbody>();
            if (rb == null)
                rb = go.AddComponent<Rigidbody>();
            rb.isKinematic = true;
            rb.useGravity = false;

            Collider col = go.GetComponentInChildren<Collider>();
            if (col == null)
            {
                SphereCollider sc = go.AddComponent<SphereCollider>();
                sc.radius = 0.3f;
                sc.isTrigger = true;
            }
            else
            {
                col.isTrigger = true;
            }

            Projectile proj = go.AddComponent<Projectile>();
            proj.Launch(group, index, startPos, dir, speed, maxRange);
        }

        // 서버 SkillComponent::computeFanDirs 와 동일한 공식/순서 (projectile_index 정합).
        // index 0 = -fan/2, ..., index count-1 = +fan/2. X-Z 평면 회전.
        private static List<Vector3> computeFanDirs(Vector3 dir, int count, float fanAngleDeg)
        {
            List<Vector3> dirs = new List<Vector3>(count);
            if (count <= 1)
            {
                dirs.Add(dir);
                return dirs;
            }

            float totalRad = fanAngleDeg * Mathf.Deg2Rad;
            float step = totalRad / (count - 1);
            float startRad = -totalRad * 0.5f;

            for (int i = 0; i < count; ++i)
            {
                float a = startRad + step * i;
                float cs = Mathf.Cos(a);
                float sn = Mathf.Sin(a);
                // X-Z 평면에서 dir 을 a 만큼 회전 (서버와 동일).
                dirs.Add(new Vector3(dir.x * cs - dir.z * sn, dir.y, dir.x * sn + dir.z * cs));
            }
            return dirs;
        }

        // ─── hit 배치 플러시 ─────────────────────────────────────────
        private void LateUpdate()
        {
            flushHits();
            sweepDoneGroups();
        }

        private void flushHits()
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
                return;

            SkillProjectileHitReq req = null;
            foreach (SkillProjectileGroup g in m_groups)
            {
                if (!g.HasEffectId)
                    continue;   // effect_id 미수신 → 버퍼 유지(다음 프레임에 재시도).

                List<SkillHitItem> drained = g.DrainPending();
                if (drained == null)
                    continue;

                if (req == null)
                    req = new SkillProjectileHitReq();
                req.Hits.AddRange(drained);
            }

            if (req != null && req.Hits.Count > 0)
                net.Send(GamePacketId.SkillProjectileHitReq, req);
        }

        private void sweepDoneGroups()
        {
            for (int i = m_groups.Count - 1; i >= 0; --i)
            {
                if (m_groups[i].IsDone && m_groups[i].HasEffectId)
                    m_groups.RemoveAt(i);
            }
        }

        // ─── DamageNtf ───────────────────────────────────────────────
        private void onSkillDamageNtf(SkillDamageNtf ntf)
        {
            ActorObject target = StageManager.Instance?.FindActor(ntf.TargetObjectId);
            if (target == null)
                return;

            target.SetCurHp(ntf.RemainingHp);

            Vector3 head = target.transform.position + Vector3.up * 1.5f;
            DamageText.Spawn(head, ntf.Damage, ntf.IsDuplicate);

            // is_dead: 1차엔 표시만. 디스폰/보상은 서버 후속(ObjectVisibilityNtf 의 Despawn 이 처리).
        }
    }
}
