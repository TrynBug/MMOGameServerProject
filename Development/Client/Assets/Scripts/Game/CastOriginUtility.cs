using UnityEngine;

namespace Client.Game
{
    /// <summary>
    /// 액터 프리팹 Root 직속 SkillCastOrigin 마커의 공용 규약입니다.
    /// 서버에는 export한 Root 기준 local XYZ만 전달한다.
    /// </summary>
    public static class CastOriginUtility
    {
        public const string TransformName = "SkillCastOrigin";
        public static readonly Vector3 DefaultLocalOffset = new Vector3(0.0f, 1.0f, 0.0f);

        public static Transform Find(Transform actorRoot)
        {
            return actorRoot != null ? actorRoot.Find(TransformName) : null;
        }

        public static bool TryGetLocalOffset(Transform origin, out Vector3 localOffset)
        {
            localOffset = DefaultLocalOffset;
            if (origin == null)
                return false;

            localOffset = origin.localPosition;
            return !float.IsNaN(localOffset.x) && !float.IsInfinity(localOffset.x)
                && !float.IsNaN(localOffset.y) && !float.IsInfinity(localOffset.y)
                && !float.IsNaN(localOffset.z) && !float.IsInfinity(localOffset.z);
        }

        public static Vector3 Resolve(Transform actorRoot, Transform origin, Vector3 castDirection)
        {
            Vector3 actorPosition = actorRoot != null ? actorRoot.position : Vector3.zero;
            Vector3 localOffset = TryGetLocalOffset(origin, out Vector3 readOffset)
                ? readOffset
                : DefaultLocalOffset;
            return ResolveLocalOffset(actorPosition, castDirection, localOffset);
        }

        public static Vector3 ResolveLocalOffset(Vector3 actorPosition, Vector3 castDirection, Vector3 localOffset)
        {
            castDirection.y = 0.0f;
            if (castDirection.sqrMagnitude < 0.0001f)
                return actorPosition;

            castDirection.Normalize();
            Vector3 right = new Vector3(castDirection.z, 0.0f, -castDirection.x);
            return actorPosition
                 + right * localOffset.x
                 + Vector3.up * localOffset.y
                 + castDirection * localOffset.z;
        }
    }
}
