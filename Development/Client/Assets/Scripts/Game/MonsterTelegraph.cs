using UnityEngine;
using GameData;

namespace Client.Game
{
    // 몬스터/NPC 공격의 바닥 텔레그래프(예고). 절차적 생성 — 아트 에셋 불필요.
    //
    // GameData_Skill 의 EffectShape/크기로 footprint 를 만들고, windupMs 동안 0→100% 로 채워(스케일)
    // 플레이어에게 "여기 맞는다"를 예고한다. windup 종료 시 스스로 파괴된다(발동 시점 ≈ 서버 대미지).
    // 실제 대미지/판정은 서버 권위(SkillDamageNtf) — 이 오브젝트는 표시 전용이다.
    //
    // ※ 임시 비주얼: 프리미티브(원=Cylinder, OBB=Cube) + 단색. 아트가 붙으면 데칼 prefab 으로 교체 예정.
    public class MonsterTelegraph : MonoBehaviour
    {
        private const float k_groundY   = 0.02f;   // z-fighting 방지용 약간 띄움
        private const float k_thickness = 0.05f;   // 바닥 데칼 두께(Y)
        private const float k_startFill = 0.15f;   // 시작 채움 비율

        private float m_fullX, m_fullZ;
        private float m_elapsed, m_windupSec;
        private Transform m_shape;

        public static void Spawn(GameData_Skill skill, Vector3 origin, Vector3 dir, int windupMs)
        {
            if (skill == null)
                return;

            // 모양/크기 결정.
            PrimitiveType prim;
            float fullX, fullZ;
            if (skill.EffectShape == ESkillEffectShape.Obb)
            {
                prim  = PrimitiveType.Cube;                       // 1x1x1 → x=ObbWidth, z=ObbLength
                fullX = Mathf.Max(0.1f, (float)skill.ObbWidth);
                fullZ = Mathf.Max(0.1f, (float)skill.ObbLength);
            }
            else
            {
                prim  = PrimitiveType.Cylinder;                   // 반지름 0.5 → scale=지름 으로 반지름 = Radius
                float diameter = Mathf.Max(0.1f, (float)skill.Radius * 2f);
                fullX = diameter;
                fullZ = diameter;
            }

            GameObject root = new GameObject("MonsterTelegraph");
            root.transform.position = new Vector3(origin.x, k_groundY, origin.z);
            if (dir.sqrMagnitude > 0.0001f)
                root.transform.rotation = Quaternion.LookRotation(new Vector3(dir.x, 0f, dir.z));

            GameObject shape = GameObject.CreatePrimitive(prim);
            Collider col = shape.GetComponent<Collider>();
            if (col != null)
                Destroy(col);
            shape.transform.SetParent(root.transform, false);

            Renderer rend = shape.GetComponent<Renderer>();
            if (rend != null)
            {
                Color c = new Color(1f, 0.2f, 0.1f, 1f);          // 위험 예고 = 붉은색
                Material m = rend.material;                        // 인스턴스 (오브젝트와 함께 파괴됨)
                if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor", c);
                if (m.HasProperty("_Color"))     m.SetColor("_Color", c);
            }

            MonsterTelegraph tg = root.AddComponent<MonsterTelegraph>();
            tg.m_fullX     = fullX;
            tg.m_fullZ     = fullZ;
            tg.m_windupSec = Mathf.Max(0.01f, windupMs / 1000f);
            tg.m_shape     = shape.transform;
            tg.apply(k_startFill);

            Destroy(root, tg.m_windupSec);   // 발동 시점에 사라짐
        }

        private void Update()
        {
            m_elapsed += Time.deltaTime;
            float t = Mathf.Clamp01(m_elapsed / m_windupSec);
            apply(Mathf.Lerp(k_startFill, 1f, t));
        }

        private void apply(float fill)
        {
            if (m_shape == null)
                return;
            m_shape.localScale = new Vector3(m_fullX * fill, k_thickness, m_fullZ * fill);
            m_shape.localPosition = Vector3.zero;
        }
    }
}
