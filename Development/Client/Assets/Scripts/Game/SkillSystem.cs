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

        [SerializeField] private int m_skill1Key = 1001;  // 파이어볼
        [SerializeField] private int m_skill2Key = 1003;  // 얼음지대
        [SerializeField] private int m_skill3Key = 1008;  // 불기둥
        [SerializeField] private int m_skill4Key = 0;

        // 단발(instant) 범위 비주얼의 표시 시간(초). 지속 데이터(LifetimeMs)가 없는 단일 틱 스킬에 사용.
        [SerializeField] private float m_areaInstantDisplaySec = 0.5f;

        // 투사체 비주얼의 Y 오프셋 (지면에 반쯤 묻히지 않도록). XZ 판정에는 영향 없음.
        [SerializeField] private float m_projectileHeight = 0.5f;

        // 다른(원격) 캐릭터가 쓴 스킬의 효과음 볼륨 배수(0~1). 내 스킬은 1.0(원래 볼륨), 원격은 이 값으로 줄여 재생.
        // 발사음(SfxShoot)·지속음(SfxLoop)·투사체 적중음(SfxHit)에 적용.
        [SerializeField] private float m_remoteSfxVolumeScale = 0.85f;

        // 시전 클립의 기본 길이(초). CastSpeed = min(1.0, 이 값 / ActionLock초).
        // '절대 안 빨라짐' 정책: 시전시간이 클립보다 짧아도 배속을 올리지 않고 1.0(원래속도)로 재생하며,
        // 긴 시전에서만 1.0 미만으로 느리게(루프 채널링) 재생한다.
        // v1 은 모든 스킬이 같은 시전 클립을 공유한다고 보고 하나로 둔다.
        // 스킬마다 시전 클립이 달라지면 Skill 데이터에 client 전용 CastAnimLength 컴럼으로 옮긴다.
        [SerializeField] private float m_castClipBaseLength = 0.8f;

        private readonly ISkillTargetingMode m_targeting = new NearestTargeting();
        private readonly ISkillTargetingMode m_highestGradeTargeting = new HighestGradeNearestTargeting();

        // 타겟형 범위 스킬의 오토타겟 검색 반경 (MaxRange=0 일 때) + 타겟 없을 때 정면 고정 시전 거리.
        [SerializeField] private float m_areaTargetRange = 30f;
        [SerializeField] private float m_areaCastFallbackDistance = 10f;

        // 시전 클라가 발사한 투사체 그룹들 (effect_id 바인딩 + hit 보고 대상).
        private readonly List<SkillProjectileGroup> m_groups = new List<SkillProjectileGroup>();

        // 다음 시전 가능 시각 (액션락). Time.time 기준.
        private float m_actionLockUntil;

        // 스킬별 쿨다운 종료 시각 (skillKey → Time.time). 시전 선체크 + HUD 라디얼 공용.
        // 클라 예측이며 서버 권위 검증은 5c. CooldownMs<=0 인 스킬은 등록하지 않는다.
        private readonly Dictionary<int, float> m_cooldownUntil = new Dictionary<int, float>();

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
            PacketDispatcher.Instance.Register<AbilityCastNtf>(GamePacketId.AbilityCastNtf, onAbilityCastNtf);

            Debug.Log("[SkillSystem] Ready.");
        }

        private void OnEnable()
        {
            InputManager input = Managers.Managers.Input;
            if (input != null)
            {
                input.OnSkill1 += castSkill1;
                input.OnSkill2 += castSkill2;
                input.OnSkill3 += castSkill3;
                input.OnSkill4 += castSkill4;
            }
        }

        private void OnDisable()
        {
            InputManager input = Managers.Managers.Input;
            if (input != null)
            {
                input.OnSkill1 -= castSkill1;
                input.OnSkill2 -= castSkill2;
                input.OnSkill3 -= castSkill3;
                input.OnSkill4 -= castSkill4;
            }
        }

        private void OnDestroy()
        {
            if (Instance == this)
                Instance = null;
        }

        // ─── 시전 ────────────────────────────────────────────────────
        private void castSkill1() => tryCast(m_skill1Key);
        private void castSkill2() => tryCast(m_skill2Key);
        private void castSkill3() => tryCast(m_skill3Key);
        private void castSkill4() => tryCast(m_skill4Key);

        // ─── HUD 조회 API (UI_PlayerHud 가 폴링) ─────────────────────
        // 스킬 슬롯 개수 (입력 OnSkill1~4 에 대응). 슬롯 키 매핑이 늘면 여기와 GetSlotSkillKey 를 함께 늘린다.
        public const int SkillSlotCount = 4;

        // 슬롯(0..SkillSlotCount-1)에 매핑된 스킬 키. 범위 밖이면 0.
        public int GetSlotSkillKey(int slot)
        {
            switch (slot)
            {
                case 0:  return m_skill1Key;
                case 1:  return m_skill2Key;
                case 2:  return m_skill3Key;
                case 3:  return m_skill4Key;
                default: return 0;
            }
        }

        // 스킬이 쿨다운 중인가. 시전 선체크 + HUD 공용.
        public bool IsOnCooldown(int skillKey)
            => m_cooldownUntil.TryGetValue(skillKey, out float until) && Time.time < until;

        // 남은 쿨다운(초). 준비 완료면 0.
        public float GetCooldownRemainSec(int skillKey)
        {
            if (m_cooldownUntil.TryGetValue(skillKey, out float until))
            {
                float remain = until - Time.time;
                return remain > 0f ? remain : 0f;
            }
            return 0f;
        }

        // 남은 쿨다운 비율 (1=방금 시전, 0=준비완료). HUD 라디얼 fillAmount 용.
        // CooldownMs<=0(쿨다운 없는 스킬)이면 항상 0.
        public float GetCooldownRatio(int skillKey)
        {
            GameData_Skill skill = GameDataTable_Skill.FindData(skillKey);
            if (skill == null || skill.CooldownMs <= 0)
                return 0f;

            float remain = GetCooldownRemainSec(skillKey);
            return Mathf.Clamp01(remain / (skill.CooldownMs / 1000f));
        }

        private void tryCast(int skillKey)
        {
            if (skillKey <= 0)
                return;

            if (Time.time < m_actionLockUntil)
                return;   // 이전 시전 애니메이션(액션락) 중.

            // 스킬별 쿨다운 중이면 시전 불가 (클라 예측, 서버 권위는 5c). HUD 라디얼과 동일 소스.
            if (IsOnCooldown(skillKey))
                return;

            GameData_Skill skill = GameDataTable_Skill.FindData(skillKey);
            if (skill == null)
            {
                Debug.LogWarning($"[SkillSystem] skill data 없음: {skillKey}");
                return;
            }

            PlayerCharacter caster = StageManager.Instance?.LocalPlayer;
            if (caster == null)
                return;

            // 사망 중이면 시전 불가 (입력 차단).
            if (caster.IsDead)
                return;

            // 마나 선체크 (클라가 먼저 막음. 서버도 검증 — 5c).
            if (caster.CurMp < skill.ManaCost)
                return;

            // 방향/타겟 결정.
            Vector3 origin = caster.transform.position;
            Vector3 dir;
            long targetId = 0;
            Vector3 targetPos = origin;

            bool isMoveSkill = skill.EffectDamage == ESkillEffectDamage.None && skill.MoveDistance > 0.0;
            if (isMoveSkill)
            {
                // 이동 스킬(글라이드/순간이동)은 오토타겟이 아니라 마우스 지면점을 향한다.
                Vector3 aim = origin + flatForward(caster);
                if (MouseInputHandler.Instance != null
                    && MouseInputHandler.Instance.TryGetMouseGroundPoint(caster, out Vector3 gp))
                    aim = gp;

                Vector3 to = aim - origin;
                to.y = 0f;
                dir = (to.sqrMagnitude > 0.0001f) ? to.normalized : flatForward(caster);
                targetPos = aim;   // 서버 클램프(min(MoveDistance, |targetPos-origin|))와 동일 입력.
            }
            else
            {
                // 1) 타게팅: Targeting 모드로 대상 선택 (None 이면 타게팅 안 함 — 전격방출 등).
                MonsterObject target = null;
                if (skill.Targeting != ETargetingMode.None)
                {
                    ISkillTargetingMode targeting = (skill.Targeting == ETargetingMode.HighestGradeNearest)
                        ? m_highestGradeTargeting : m_targeting;
                    float searchRange = (skill.MaxRange > 0.0) ? (float)skill.MaxRange : m_areaTargetRange;
                    target = targeting.SelectTarget(origin, searchRange);
                }

                // 2) 방향: 타겟 있으면 캐스터→타겟, 없으면 캐스터 정면.
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

                // 3) 배치(Placement): 효과 중심 origin 결정. (서버는 이 origin 을 그대로 효과 중심으로 사용)
                switch (skill.Placement)
                {
                    case ESkillPlacement.Target:
                        // 타겟 위치. 타겟 없으면 정면 고정거리에 시전(설계).
                        origin = (target != null)
                            ? target.transform.position
                            : caster.transform.position + dir * m_areaCastFallbackDistance;
                        targetPos = origin;
                        break;
                    case ESkillPlacement.Forward:
                        // 캐스터 전방에서 타겟 방향으로 OBB: 중심을 정면으로 ObbLength/2 만큼 밀어 빔이 앞으로 뻗게.
                        origin = caster.transform.position + dir * (float)(skill.ObbLength * 0.5);
                        break;
                    // None / Caster: origin = 캐스터 위치 (투사체 발사점 / 캐스터 중심). 그대로 둔다.
                }
            }

            // 캐릭터 즉시 회전 (Rotation=true 인 스킬).
            if (skill.Rotation)
                caster.transform.rotation = Quaternion.LookRotation(dir);

            // 시전 애니메이션 재생 (CastAnim 이 있으면).
            float actionLockSec = skill.ActionLockMs / 1000f;
            // '절대 안 빨라짐': 최대 1.0 으로 클램프. 짧은 시전은 원래속도(부분 재생), 긴 시전만 느려진다.
            float castSpeed = (m_castClipBaseLength > 0f && actionLockSec > 0.01f)
                ? Mathf.Min(1f, m_castClipBaseLength / actionLockSec)
                : 1f;

            // 캐스팅(홀드): 직업/클래스 무관하게 스킬 데이터 CastAnim 상태로 진입(마지막 프레임에서 대기).
            caster.PlayCast(skill.CastAnim, castSpeed);
            // 발동(FireAnim): CastDelay 경과(발동 순간)에 재생 → 종료 시 Locomotion 복귀.
            StartCoroutine(playFireAnimAfterDelay(caster, skill, castSpeed));

            // Stationary 는 시전 동안 이동 잠금. (Mobile 은 이동 유지 → 잠그지 않음)
            if (skill.CastClass == ESkillCastClass.Stationary)
                caster.LockMovement(actionLockSec);

            // 액션락 설정 (다음 시전까지 잠금).
            m_actionLockUntil = Time.time + skill.ActionLockMs / 1000f;

            // 스킬별 쿨다운 시작 (클라 예측). HUD 라디얼 + 다음 시전 선체크 공용.
            if (skill.CooldownMs > 0)
                m_cooldownUntil[skillKey] = Time.time + skill.CooldownMs / 1000f;

            // 시전음 (클라 전용). 데이터 SfxCast 경로 prefix 의 변형을 라운드로빈+피치로 재생.
            SfxPlayer.Play(skill.SfxCast, caster.transform.position);
            m_skillUseTime[skillKey] = Time.time;   // 스킬 사용 시각 기록 (SfxHitIgnoreMs: 사용 직후 적중음 억제용).

            // 위치표시(telegraph) VFX (클라 전용). 착탄 origin 에 표시, 임팩트(CastDelay+FirstTickDelay)까지 유지 후 제거.
            spawnTelegraph(skill, origin, (skill.CastDelayMs + skill.FirstTickDelayMs) / 1000f);

            // 서버에 시전 요청 즉시 송신 (서버는 자기 CastDelay 후 entry 발동).
            sendCastReq(skill, origin, dir, targetId, targetPos);

            // 투사체 스킬이면 예측 발사 (CastDelay 후). 그룹은 즉시 등록(effect_id 바인딩 대기).
            if (skill.EffectDamage == ESkillEffectDamage.ContactHit)
            {
                SkillProjectileGroup group = new SkillProjectileGroup(skillKey);
                m_groups.Add(group);
                StartCoroutine(spawnProjectilesAfterDelay(group, skill, origin, dir));
            }
            // 범위 스킬이면 CastDelay 후 비주얼만 예측 표시. 대미지는 서버 SkillDamageNtf 가 구동한다.
            else if (skill.EffectDamage == ESkillEffectDamage.Area)
            {
                StartCoroutine(spawnAreaVisualAfterDelay(skill, origin, dir));
            }
            // 이동 스킬이면 강제이동을 로컬 예측한다 (서버와 동일 공식). 글라이드는 충격파 페이즈도 재생.
            else if (isMoveSkill)
            {
                predictSkillMovement(caster, skill, origin, dir, targetPos);
            }
        }

        // 캐릭터 정면(수평). 정면이 거의 수직이면 월드 forward 폴백.
        private static Vector3 flatForward(PlayerCharacter caster)
        {
            Vector3 fwd = caster.transform.forward;
            fwd.y = 0f;
            return (fwd.sqrMagnitude > 0.0001f) ? fwd.normalized : Vector3.forward;
        }

        // 발동 애니메이션(FireAnim)을 CastDelay 경과 후(발동 순간) 재생. 캐스팅(CastAnim, 홀드) → 발동 모션 전환.
        // FireAnim 이 비어있으면 PlayFire 내부에서 무시된다.
        private IEnumerator playFireAnimAfterDelay(PlayerCharacter caster, GameData_Skill skill, float castSpeed)
        {
            if (skill.CastDelayMs > 0)
                yield return new WaitForSeconds(skill.CastDelayMs / 1000f);
            if (caster != null)
                caster.PlayFire(skill.FireAnim, castSpeed);
        }

        private IEnumerator spawnProjectilesAfterDelay(SkillProjectileGroup group, GameData_Skill skill, Vector3 origin, Vector3 dir)
        {
            if (skill.CastDelayMs > 0)
                yield return new WaitForSeconds(skill.CastDelayMs / 1000f);

            // 발동음 (클라 전용). 시전 발동(CastDelay 경과) 순간 SfxShoot 재생.
            SfxPlayer.Play(skill.SfxShoot, origin);
            spawnFan(group, skill, origin, dir);
        }

        // 범위 스킬 비주얼을 CastDelay 후에 표시한다 (로컬 예측). 대미지는 서버가 구동.
        private IEnumerator spawnAreaVisualAfterDelay(GameData_Skill skill, Vector3 origin, Vector3 dir)
        {
            if (skill.CastDelayMs > 0)
                yield return new WaitForSeconds(skill.CastDelayMs / 1000f);

            // 발동음 (클라 전용). 시전 발동(CastDelay 경과) 순간 SfxShoot 재생. (entry 1회 — 체인 페이즈는 제외.)
            SfxPlayer.Play(skill.SfxShoot, origin);
            // 지속음 (클라 전용). 운석 낙하 등 — entry 페이즈 지속시간 동안 루프 후 정지(임팩트 시).
            SfxPlayer.PlayLoop(skill.SfxLoop, origin, areaPhaseDurationSec(skill));
            spawnAreaChainPhase(skill, origin, dir);
        }

        // 위치표시(telegraph) VFX 를 origin 에 스폰하고 lifetimeSec 후 자동 제거한다. TelegraphPrefabPath 가 있는 스킬만.
        private void spawnTelegraph(GameData_Skill skill, Vector3 origin, float lifetimeSec)
        {
            if (string.IsNullOrEmpty(skill.TelegraphPrefabPath) || lifetimeSec <= 0f)
                return;

            GameObject prefab = Managers.Managers.Resource.Load<GameObject>(skill.TelegraphPrefabPath);
            if (prefab == null)
                return;

            GameObject go = Instantiate(prefab);
            go.name = "Telegraph_" + skill.Name;
            go.transform.position = origin;

            // 임팩트 반경(Radius)에 맞춰 Circle 스케일 (area 비주얼과 동일 규약).
            AreaVfx vfx = go.GetComponent<AreaVfx>();
            float baseD = (vfx != null && vfx.baseDiameter > 0f) ? vfx.baseDiameter : 1f;
            float scale = ((float)skill.Radius * 2f) / baseD;
            Vector3 sc = go.transform.localScale;
            go.transform.localScale = new Vector3(scale, scale, scale);

            Destroy(go, lifetimeSec);   // lifetimeSec(≈임팩트) 후 제거
        }

        // 범위(Area) 스킬을 origin 에 표시한다 (로컬 예측/원격 재현 공용).
        // ScatterCount>1 이면 [inner,outer] 링에 N개를 흩뿌린다 (메테오 파편).
        // 흩뿌리는 위치는 클라 로컬 랜덤(장식) — 대미지는 서버가 enemy 위치로 판정하므로 DamageNtf 가 정확도를 보장.
        private void spawnAreaVisual(GameData_Skill skill, Vector3 origin, Vector3 dir)
        {
            if (skill.ScatterCount > 1)
            {
                int n = (int)skill.ScatterCount;
                float inner = (float)skill.ScatterInnerRadius;
                float outer = (float)skill.ScatterOuterRadius;
                for (int i = 0; i < n; ++i)
                {
                    float ang = UnityEngine.Random.Range(0f, Mathf.PI * 2f);
                    // 면적 균등: r = sqrt(U[inner^2, outer^2]). 서버와 같은 분포 형태(시드 미공유라 위치 자체는 다름).
                    float r = Mathf.Sqrt(UnityEngine.Random.Range(inner * inner, outer * outer));
                    Vector3 p = origin + new Vector3(Mathf.Cos(ang) * r, 0f, Mathf.Sin(ang) * r);
                    spawnOneAreaVisual(skill, p, dir);
                }
                return;
            }

            spawnOneAreaVisual(skill, origin, dir);
        }

        // 범위 비주얼 prefab 1개를 pos 에 생성. shape 회전(Obb)·스케일 후 표시시간 뒤 파괴. 대미지는 서버 DamageNtf 담당.
        private void spawnOneAreaVisual(GameData_Skill skill, Vector3 pos, Vector3 dir)
        {
            // 비주얼 prefab 은 스킬 데이터의 EffectPrefabPath 에서 로드 (파이어볼의 ProjectilePrefabPath 와 동일 방식).
            GameObject prefab = Managers.Managers.Resource.Load<GameObject>(skill.EffectPrefabPath);
            if (prefab == null)
                return;   // prefab 없으면 ResourceManager 가 경고 1회. 폴백 없음(데이터/에셋이 맞아야 동작).

            GameObject go = Instantiate(prefab);
            go.name = "AreaVfx_" + skill.Name;
            go.transform.position = pos;

            // prefab 의 기본 크기 메타데이터 (없으면 base=1 = 1유닛 footprint 전제).
            AreaVfx vfx = go.GetComponent<AreaVfx>();

            // 모양에 맞춰 배치. 데이터 크기 / prefab 기본크기 비율로 스케일. 판정은 XZ 평면이라 Y 스케일은 prefab 값 유지.
            if (skill.EffectShape == ESkillEffectShape.Obb)
            {
                // 시전 방향으로 회전 (forward = dir). OBB 가로=ObbWidth(X), 세로=ObbLength(Z).
                if (dir.sqrMagnitude > 0.0001f)
                    go.transform.rotation = Quaternion.LookRotation(new Vector3(dir.x, 0f, dir.z));

                float baseW = (vfx != null && vfx.baseSize.x > 0f) ? vfx.baseSize.x : 1f;
                float baseL = (vfx != null && vfx.baseSize.y > 0f) ? vfx.baseSize.y : 1f;
                Vector3 s = go.transform.localScale;
                go.transform.localScale = new Vector3((float)skill.ObbWidth / baseW, s.y, (float)skill.ObbLength / baseL);
            }
            else if (skill.EffectShape == ESkillEffectShape.Circle)
            {
                // 지름 = 반지름 × 2. prefab 기본 지름(baseDiameter) 대비 비율로 스케일.
                float baseD = (vfx != null && vfx.baseDiameter > 0f) ? vfx.baseDiameter : 1f;
                float scale = ((float)skill.Radius * 2f) / baseD;
                Vector3 s = go.transform.localScale;
                go.transform.localScale = new Vector3(scale, scale, scale);
            }

            // 표시 시간: 지속형(periodic)은 LifetimeMs, 단발(instant)은 FirstTickDelay(낙하 등 리드업) + 고정 표시.
            float displaySec = (skill.LifetimeMs > 0)
                ? skill.LifetimeMs / 1000f
                : skill.FirstTickDelayMs / 1000f + m_areaInstantDisplaySec;
            Destroy(go, displaySec);
        }

        // ─── 다중 Area 체인 재생 (블레이즈 빔→화염지대, 메테오 운석→파편) ───
        // 현재 Area 페이즈를 표시하고, 체인(NextSkillKey)이 있으면 다음 Area 페이즈를 예약한다.
        private void spawnAreaChainPhase(GameData_Skill skill, Vector3 center, Vector3 dir)
        {
            spawnAreaVisual(skill, center, dir);

            if (skill.NextSkillKey == 0 || skill.NextTriggerTiming == ENextSkillTiming.None)
                return;

            GameData_Skill next = GameDataTable_Skill.FindData(skill.NextSkillKey);
            if (next == null)
                return;

            float wait = areaPhaseDurationSec(skill);   // AfterEnd: 이 페이즈 지속 종료 직후
            if (skill.NextTriggerTiming == ENextSkillTiming.AfterDelay)
                wait += skill.NextTriggerDelayMs / 1000f;

            Vector3 nextCenter = resolveAreaChainOrigin(skill, center, dir);
            StartCoroutine(spawnAreaChainAfter(next, nextCenter, dir, wait));
        }

        private IEnumerator spawnAreaChainAfter(GameData_Skill skill, Vector3 center, Vector3 dir, float wait)
        {
            if (wait > 0f)
                yield return new WaitForSeconds(wait);
            spawnAreaChainPhase(skill, center, dir);   // 재귀 (다음 페이즈도 체인 가능)
        }

        // Area 페이즈의 체인 지속시간(초). 서버 chainDurationMs(Area) = max(FirstTickDelay, Lifetime) 와 동일.
        private static float areaPhaseDurationSec(GameData_Skill skill)
        {
            long ms = Mathf.Max((int)skill.FirstTickDelayMs, (int)skill.LifetimeMs);
            return ms / 1000f;
        }

        // 직전 Area 페이즈의 NextOrigin 규칙으로 다음 페이즈 중심을 구한다.
        // Area(Static)는 중심=끝=시작이라 PrevCenter/PrevEnd 모두 현재 center.
        private Vector3 resolveAreaChainOrigin(GameData_Skill skill, Vector3 center, Vector3 dir)
        {
            switch (skill.NextOrigin)
            {
                case ENextSkillOrigin.CasterPos:
                {
                    PlayerCharacter local = StageManager.Instance?.LocalPlayer;
                    return local != null ? local.transform.position : center;
                }
                case ENextSkillOrigin.CasterFront: return center + dir * (float)skill.CasterFrontDistance;
                default:                           return center;   // PrevCenter / PrevEnd / None
            }
        }

        // ─── 이동 스킬 예측 + 체인 재생 ──────────────────────────────
        // 이동 스킬(글라이드/순간이동) 로컬 예측. 서버 Character::ApplySkillMovement 와 동일 규칙.
        //   - 강제 대시(MoveDurationMs>0): 고정 거리 ease-out.
        //   - 즉시 블링크(MoveDurationMs<=0): min(MoveDistance, |aim-origin|) 만큼 즉시 이동.
        // NextSkillKey 가 있으면(글라이드) 이동 종료 후 다음(충격파) 페이즈 비주얼을 재생한다.
        private void predictSkillMovement(PlayerCharacter caster, GameData_Skill skill, Vector3 origin, Vector3 dir, Vector3 aim)
        {
            // 발동음 (클라 전용). 이동기 발동 순간 SfxShoot 재생.
            SfxPlayer.Play(skill.SfxShoot, origin);

            float distance;
            if (skill.MoveDurationMs > 0)
            {
                distance = (float)skill.MoveDistance;   // 글라이드: 고정 거리
            }
            else
            {
                Vector3 to = aim - origin;
                to.y = 0f;
                distance = Mathf.Min((float)skill.MoveDistance, to.magnitude);   // 순간이동: 클램프
            }

            float durationSec = skill.MoveDurationMs / 1000f;
            caster.StartSkillMove(dir, distance, durationSec);

            // 이동 페이즈 뒤에 이어지는 체인(글라이드 충격파 등) 비주얼 재생.
            if (skill.NextSkillKey != 0 && skill.NextTriggerTiming != ENextSkillTiming.None)
                StartCoroutine(spawnChainAfterMove(skill, origin, dir, distance, durationSec));
        }

        // 이동 종료 시각에 맞춰 다음 페이즈(Area) 비주얼을 그 origin 에 표시한다.
        private IEnumerator spawnChainAfterMove(GameData_Skill skill, Vector3 origin, Vector3 dir, float distance, float durationSec)
        {
            float wait = durationSec;   // AfterEnd: 이동(지속) 종료 직후
            if (skill.NextTriggerTiming == ENextSkillTiming.AfterDelay)
                wait += skill.NextTriggerDelayMs / 1000f;
            if (wait > 0f)
                yield return new WaitForSeconds(wait);

            GameData_Skill next = GameDataTable_Skill.FindData(skill.NextSkillKey);
            if (next == null)
                yield break;

            Vector3 nextOrigin = resolveChainOrigin(skill, origin, dir, distance);
            if (next.EffectDamage == ESkillEffectDamage.Area)
                spawnAreaChainPhase(next, nextOrigin, dir);
            // (다음 페이즈가 또 체인이면 재귀 확장 — v1 글라이드는 1단계라 생략.)
        }

        // 직전 페이즈(이동)의 NextOrigin 규칙으로 다음 페이즈 기준 위치를 구한다.
        private Vector3 resolveChainOrigin(GameData_Skill skill, Vector3 origin, Vector3 dir, float distance)
        {
            switch (skill.NextOrigin)
            {
                case ENextSkillOrigin.CasterPos:
                {
                    PlayerCharacter local = StageManager.Instance?.LocalPlayer;
                    return local != null ? local.transform.position : origin;
                }
                case ENextSkillOrigin.CasterFront: return origin + dir * (float)skill.CasterFrontDistance;
                case ENextSkillOrigin.PrevEnd:     return origin + dir * distance;   // 이동 끝점
                case ENextSkillOrigin.PrevCenter:  return origin;                     // 이동 중심=시작(v1)
                default:                           return origin;
            }
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
                ? StageManager.Instance.LocalPlayer.ObjectId
                : 0;

            if (ntf.CasterObjectId == localId)
            {
                // 내 시전: 돌아온 effect_id 를 예측 발사한 그룹(가장 오래된 미바인딩 동일 스킬)에 바인딩.
                // (이미 예측 발사했으므로 여기서 다시 스폰하지 않는다.)
                bindEffectId(ntf.SkillKey, ntf.EffectId);
                return;
            }

            // 다른 캐스터(플레이어): 발동(fire) 모션 재생. SkillCastNtf 는 CastDelay 경과(발동) 시점이라 즉시 발동 모션.
            // (몬스터는 as PlayerCharacter 실패로 skip — 몬스터 시전연출은 AbilityCastNtf 가 담당.)
            PlayerCharacter remoteCaster = StageManager.Instance?.FindActor(ntf.CasterObjectId) as PlayerCharacter;
            GameData_Skill castSkill = GameDataTable_Skill.FindData(ntf.SkillKey);
            if (remoteCaster != null && castSkill != null)
                remoteCaster.PlayFire(castSkill.FireAnim, computeCastSpeed(castSkill));

            // 비주얼 전용 투사체/효과 재현 (보고 안 함).
            spawnRemoteVisual(ntf);
        }

        // 시전/발동 애니 재생속도 계산. '절대 안 빨라짐' 정책(최대 1.0 클램프)과 동일.
        private float computeCastSpeed(GameData_Skill skill)
        {
            float actionLockSec = skill.ActionLockMs / 1000f;
            return (m_castClipBaseLength > 0f && actionLockSec > 0.01f)
                ? Mathf.Min(1f, m_castClipBaseLength / actionLockSec)
                : 1f;
        }

        // ─── AbilityCastNtf (몬스터/NPC 시전 "시작") ──────────────────
        // 플레이어 SkillCastNtf 가 "발동(효과 재현)"인 것과 달리, 이건 "시작(예고)"이다.
        // 시전자 회전+윈드업 모션 + 바닥 텔레그래프를 windup_ms 동안 재생한다.
        // 실제 대미지/효과는 뒤따르는 SkillDamageNtf (+투사체는 후속 SkillCastNtf)가 권위.
        private void onAbilityCastNtf(AbilityCastNtf ntf)
        {
            Vector3 dir = new Vector3(ntf.DirX, 0f, ntf.DirZ);

            ActorObject caster = StageManager.Instance != null ? StageManager.Instance.FindActor(ntf.CasterObjectId) : null;

            if (caster is MonsterObject monster)
            {
                // 몬스터/NPC: 윈드업 모션 + 회전 + 바닥 텔레그래프(예고).
                GameData_Skill skill = GameDataTable_Skill.FindData(ntf.SkillKey);
                monster.PlayAbilityCast(dir, skill);
                if (ntf.WindupMs > 0 && skill != null)
                    MonsterTelegraph.Spawn(skill, new Vector3(ntf.OriginX, ntf.OriginY, ntf.OriginZ), dir, ntf.WindupMs);
            }
            else if (caster is PlayerCharacter player)
            {
                // 원격 플레이어: 캐스팅(윈드업) 모션. 발동은 뒤따르는 SkillCastNtf 가 PlayFire 로 재생한다. (텔레그래프 없음)
                GameData_Skill skill = GameDataTable_Skill.FindData(ntf.SkillKey);
                if (skill != null)
                    player.PlayCast(skill.CastAnim, computeCastSpeed(skill));
            }
        }

        private void bindEffectId(int skillKey, long effectId)
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
            if (skill == null)
                return;

            Vector3 origin = new Vector3(ntf.OriginX, ntf.OriginY, ntf.OriginZ);
            Vector3 dir = new Vector3(ntf.DirX, 0f, ntf.DirZ);
            dir = (dir.sqrMagnitude > 0.0001f) ? dir.normalized : Vector3.forward;

            // 효과음 볼륨 배수: '다른 플레이어(원격)' 스킬만 줄이고, 몬스터 스킬은 원래 볼륨 유지(피격 예고/피드백 중요).
            // (내 시전은 onSkillCastNtf 초입에서 이미 return 되므로 여기 오는 건 원격 플레이어 또는 몬스터.)
            bool casterIsMonster = StageManager.Instance?.FindActor(ntf.CasterObjectId) is MonsterObject;
            float sfxScale = casterIsMonster ? 1f : m_remoteSfxVolumeScale;

            // 발동음 (클라 전용). 원격 캐스터의 발동(서버 CastNtf=CastDelay 경과) 순간 SfxShoot 재생. 모든 타입 공용·entry 1회.
            SfxPlayer.Play(skill.SfxShoot, origin, sfxScale);

            // 위치표시(telegraph) VFX (클라 전용). 원격은 발동(CastNtf)부터 임팩트(FirstTickDelay)까지.
            spawnTelegraph(skill, origin, skill.FirstTickDelayMs / 1000f);

            // 서버는 entry 발동(=CastDelay 경과) 시점에 CastNtf 를 보내므로 원격은 즉시 재현한다.
            if (skill.EffectDamage == ESkillEffectDamage.ContactHit)
            {
                // 몬스터가 쏜 투사체면 몬스터 충돌 무시(시전자 자가충돌 방지 + 서버 권위 판정).
                spawnFan(group: null, skill, origin, dir, ignoreMonsters: casterIsMonster, sfxVolumeScale: sfxScale);   // group=null → 비주얼 전용.
            }
            else if (skill.EffectDamage == ESkillEffectDamage.Area)
            {
                // 지속음 (클라 전용). 원격 entry 페이즈 지속시간 동안 루프. 원격 캐스터라 볼륨 감소.
                SfxPlayer.PlayLoop(skill.SfxLoop, origin, areaPhaseDurationSec(skill), sfxScale);
                spawnAreaChainPhase(skill, origin, dir);
            }
            // 이동 스킬: 원격 캐스터를 서버가 실제 사용한 거리(move_distance)로 동일하게 이동시킨다.
            else if (skill.EffectDamage == ESkillEffectDamage.None && skill.MoveDistance > 0.0)
            {
                PlayerCharacter caster = StageManager.Instance?.FindActor(ntf.CasterObjectId) as PlayerCharacter;
                if (caster != null)
                {
                    float distance = ntf.MoveDistance;
                    float durationSec = skill.MoveDurationMs / 1000f;
                    caster.StartSkillMove(dir, distance, durationSec);

                    // 글라이드 등: 이동 종료 후 충격파(Area) 페이즈 비주얼 재생.
                    if (skill.NextSkillKey != 0 && skill.NextTriggerTiming != ENextSkillTiming.None)
                        StartCoroutine(spawnChainAfterMove(skill, origin, dir, distance, durationSec));
                }
            }
        }

        // ─── 투사체 발사 (로컬/원격 공용) ────────────────────────────
        private void spawnFan(SkillProjectileGroup group, GameData_Skill skill, Vector3 origin, Vector3 dir, bool ignoreMonsters = false, float sfxVolumeScale = 1f)
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
                spawnOneProjectile(prefab, group, i, startPos, dirs[i], (float)skill.ProjectileSpeed, (float)skill.MaxRange, skill.Key, skill.OnHitSkillKey, ignoreMonsters, sfxVolumeScale);

            group?.MarkLaunched(dirs.Count);
        }

        private void spawnOneProjectile(GameObject prefab, SkillProjectileGroup group, int index, Vector3 startPos, Vector3 dir, float speed, float maxRange, int sourceSkillKey, int onHitSkillKey, bool ignoreMonsters = false, float sfxVolumeScale = 1f)
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
                col = sc;
            }
            else
            {
                col.isTrigger = true;
            }
            // 투사체 콜라이더를 Projectile 레이어로. (지형 차단은 코드 레이캐스트로 하고,
            //  몬스터 hit 은 OnTriggerEnter 로 한다. 레이어 분리로 추후 충돌 매트릭스 최적화 여지.)
            col.gameObject.layer = GameLayers.Projectile;

            Projectile proj = go.AddComponent<Projectile>();
            proj.Launch(group, index, startPos, dir, speed, maxRange, sourceSkillKey, onHitSkillKey, ignoreMonsters, sfxVolumeScale);
        }

        // 투사체 종료(hit) 위치에서 적중음(클라 0지연) + OnHit 폭발 비주얼을 낸다. Projectile 이 종료 시 호출(시전 클라/원격 공용).
        // 폭발 적중/대미지는 서버가 판정해 SkillDamageNtf 로 통보 — 여기서는 비주얼/사운드만.
        public void SpawnHitExplosionVisual(int sourceSkillKey, int explosionSkillKey, Vector3 pos, Vector3 dir, float sfxVolumeScale = 1f)
        {
            // 적중음 (클라 전용). 투사체 hit 은 클라가 판정하므로 서버 SkillDamageNtf 를 기다리지 않고
            // hit 판정 순간에 0지연으로 source 스킬(투사체 본체)의 SfxHit 을 재생한다. (onSkillDamageNtf 에선 중복 재생 안 함.)
            // 단, 스킬 사용 직후 SfxHitIgnoreMs 동안은 억제한다(선행음과 겹침 방지).
            GameData_Skill source = GameDataTable_Skill.FindData(sourceSkillKey);
            if (source != null && !isHitSfxSuppressed(source))
                SfxPlayer.Play(source.SfxHit, pos, sfxVolumeScale);

            // OnHit 폭발 비주얼 (있으면).
            if (explosionSkillKey == 0)
                return;
            GameData_Skill explosion = GameDataTable_Skill.FindData(explosionSkillKey);
            if (explosion == null)
                return;
            spawnAreaVisual(explosion, pos, dir);
        }

        // 스킬 사용(시전) 시각(skillKey → Time.time). SfxHitIgnoreMs 에 사용.
        private readonly Dictionary<int, float> m_skillUseTime = new Dictionary<int, float>();

        // 스킬 사용 직후 SfxHitIgnoreMs 동안은 그 스킬의 적중음을 재생하지 않는다.
        // (즉발 틱 장판 등에서 시전음/발사음과 첫 적중음이 겹치는 것 방지.)
        private bool isHitSfxSuppressed(GameData_Skill skill)
        {
            if (skill.SfxHitIgnoreMs <= 0)
                return false;
            if (!m_skillUseTime.TryGetValue(skill.Key, out float useTime))
                return false;
            return (Time.time - useTime) * 1000f < skill.SfxHitIgnoreMs;
        }

        // 클라가 hit 을 직접 판정·재생하는 스킬인가? (투사체 직격=ContactHit, 그 투사체의 OnHit 폭발=아래 set)
        // 이런 스킬의 적중음은 SpawnHitExplosionVisual 에서 0지연으로 재생하므로 SkillDamageNtf 에선 재생하지 않는다.
        private HashSet<int> m_onHitSkillKeys;
        private bool isClientPredictedHit(GameData_Skill skill)
        {
            if (skill.EffectDamage == ESkillEffectDamage.ContactHit)
                return true;

            if (m_onHitSkillKeys == null)
            {
                m_onHitSkillKeys = new HashSet<int>();
                foreach (GameData_Skill s in GameDataTable_Skill.GetDataMap().Values)
                    if (s.OnHitSkillKey != 0)
                        m_onHitSkillKeys.Add(s.OnHitSkillKey);
            }
            return m_onHitSkillKeys.Contains(skill.Key);
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

            // 적중음 (클라 전용). 단, 투사체/폭발은 클라가 hit 을 판정해 SpawnHitExplosionVisual 에서 이미 재생하므로
            // 여기서는 서버 권위 적중(장판·즉발 등 클라 예측이 없는 스킬)만 재생한다.
            // 또한 스킬 사용 직후 SfxHitIgnoreMs 동안은 적중음을 억제한다(선행음과 겹침 방지).
            GameData_Skill srcSkill = GameDataTable_Skill.FindData(ntf.SourceSkillKey);
            if (srcSkill != null && !isClientPredictedHit(srcSkill) && !isHitSfxSuppressed(srcSkill))
                SfxPlayer.Play(srcSkill.SfxHit, target.transform.position);

            // 피격 대상이 몬스터면 타격감 juice(클라 전용, 서버 무관) — 적중 프레임에 flash + hitstop + 스케일팝 동시 발화.
            // 중복타격(감쇠)은 더 짧게. 가해자(플레이어)는 얼리지 않으므로 몬스터 대상에만 적용한다.
            if (target is MonsterObject hitMonster)
            {
                HitFlash.Trigger(hitMonster);
                HitStop.Trigger(hitMonster, ntf.IsDuplicate ? 50f : 100f);
                ScalePop.Play(hitMonster);
                hitMonster.PlayHitReaction();   // 피격 애니 (드라이버가 Locomotion 중 + 쓰로틀 게이트)
            }
            else if (target is PlayerCharacter hitPlayer)
            {
                hitPlayer.PlayHit();            // 로컬/원격 플레이어 피격 애니 (동일 게이트)
            }

            // 본인(LocalPlayer)이 맞았으면 피격 피드백: 공격자 방향 표식 + 카메라 셰이크(타격감).
            // attacker_object_id/source_skill_key 는 서버가 채워 보낸다(SkillDamageNtf 확장).
            if (target == StageManager.Instance.LocalPlayer && ntf.AttackerObjectId != 0)
            {
                ActorObject attacker = StageManager.Instance.FindActor(ntf.AttackerObjectId);
                if (attacker != null)
                {
                    HitDirectionIndicator.Spawn(target.transform.position, attacker.transform.position);
                    Camera.main?.GetComponent<CameraFollow>()?.AddShake(0.15f);
                }
            }

            // 사망 연출/디스폰은 이 패킷이 아니라 ObjectDeathNtf(사망) + ObjectVisibilityNtf의 Despawn(제거)이 처리한다.
        }
    }
}
