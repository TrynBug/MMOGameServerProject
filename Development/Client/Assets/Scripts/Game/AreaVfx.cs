using UnityEngine;

namespace Client.Game
{
    // 범위(Area) 이펙트 prefab 의 "기본 크기" 메타데이터.
    //
    // SkillSystem.spawnOneAreaVisual 이 스킬 데이터(Radius / ObbWidth / ObbLength)에 맞춰
    // 이펙트 스케일을 보정할 때 이 값을 기준으로 사용한다.
    //
    // prefab 을 아무 크기로 제작해도, 여기에 "localScale 1배일 때의 XZ footprint" 를 입력하면
    // 코드가 데이터 크기에 맞게 자동으로 스케일한다.
    //   - Circle 이펙트: baseDiameter (지름)
    //   - OBB 이펙트:    baseSize (가로 x, 세로 z)
    //
    // 이 컴포넌트가 prefab 에 없으면 base = 1 로 간주한다 (= 1유닛 footprint 전제, 기존 동작과 동일).
    public class AreaVfx : MonoBehaviour
    {
        [Tooltip("Circle 이펙트의 기본 지름. localScale (1,1,1) 일 때의 XZ 지름(월드 유닛).")]
        public float baseDiameter = 1f;

        [Tooltip("OBB 이펙트의 기본 크기. localScale (1,1,1) 일 때의 (가로 x, 세로 z)(월드 유닛).")]
        public Vector2 baseSize = Vector2.one;
    }
}
