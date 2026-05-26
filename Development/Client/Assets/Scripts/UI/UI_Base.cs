using System;
using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace Client.UI
{
    // 모든 UI 컴포넌트의 베이스.
    //
    // Bind 패턴:
    //   - UI 안의 자식 GameObject 들을 enum 이름으로 식별해서 자동 바인딩
    //   - 인스펙터에서 일일이 드래그 안 해도 됨
    //   - enum 이름과 자식 GameObject 이름이 같아야 함
    //
    // 사용 예:
    //   enum Buttons { CloseButton, ConfirmButton }
    //   enum Texts   { TitleText, MessageText }
    //
    //   public override void Init()
    //   {
    //       Bind<Button>(typeof(Buttons));
    //       Bind<TextMeshProUGUI>(typeof(Texts));
    //
    //       GetButton((int)Buttons.CloseButton).gameObject.BindEvent(OnCloseClicked);
    //       GetText((int)Texts.TitleText).text = "Hello";
    //   }
    public abstract class UI_Base : MonoBehaviour
    {
        // UI 이벤트 타입.
        // 별도 Define.cs 를 만들지 않고 UI_Base 안에 nested enum 으로 둠.
        // Drag 는 인벤토리 아이템 드래그 같은 곳에서 사용 예정.
        public enum UIEvent
        {
            Click,
            Drag,
        }

        // 타입별로 바인딩된 오브젝트 배열. enum 인덱스로 접근.
        private readonly Dictionary<Type, UnityEngine.Object[]> m_objects = new Dictionary<Type, UnityEngine.Object[]>();

        public abstract void Init();

        // 입력받은 enum 의 각 값에 대해, 그 이름과 동일한 자식 GameObject 를 찾아
        // T 타입 컴포넌트를 배열에 저장한다.
        //
        // 예) Bind<Button>(typeof(Buttons))
        //     -> enum Buttons 의 각 값 이름과 동일한 자식을 찾아서 Button 컴포넌트를 저장.
        //     -> Get<Button>((int)Buttons.CloseButton) 로 꺼내쓰기.
        //
        // 이름이 같은 자식이 없으면 Debug.LogWarning. (해당 인덱스는 null)
        protected void Bind<T>(Type type) where T : UnityEngine.Object
        {
            string[] names = Enum.GetNames(type);
            UnityEngine.Object[] objects = new UnityEngine.Object[names.Length];
            m_objects.Add(typeof(T), objects);

            for (int i = 0; i < names.Length; i++)
            {
                if (typeof(T) == typeof(GameObject))
                    objects[i] = findChild(gameObject, names[i], recursive: true);
                else
                    objects[i] = findChild<T>(gameObject, names[i], recursive: true);

                if (objects[i] == null)
                    Debug.LogWarning($"[UI_Base] Failed to bind {typeof(T).Name}: name={names[i]}, owner={gameObject.name}");
            }
        }

        // UI 안의 T 타입 컴포넌트 중 idx 번째를 가져온다.
        protected T Get<T>(int idx) where T : UnityEngine.Object
        {
            if (!m_objects.TryGetValue(typeof(T), out UnityEngine.Object[] objects))
                return null;

            if (idx < 0 || idx >= objects.Length)
                return null;

            return objects[idx] as T;
        }

        // 자주 쓰는 타입은 헬퍼.
        protected GameObject       GetObject(int idx) => Get<GameObject>(idx);
        protected TextMeshProUGUI  GetText(int idx)   => Get<TextMeshProUGUI>(idx);
        protected Button           GetButton(int idx) => Get<Button>(idx);
        protected Image            GetImage(int idx)  => Get<Image>(idx);

        // 임의의 GameObject 에 UI_EventHandler 를 붙이고, 지정 이벤트에 콜백 등록.
        //
        // 정적 메서드로 둔 이유: Button 외에 일반 GameObject (예: Image, Panel) 에도 클릭/드래그를
        // 받게 하려면 UI_EventHandler 가 필요한데, 이걸 호출 지점에서 매번 GetOrAddComponent 하는 게
        // 번거로워서 정적 헬퍼로 캡슐화.
        //
        // 중복 등록 방지를 위해 먼저 -= 한 다음 += 한다.
        public static void BindEvent(GameObject go, Action<PointerEventData> action, UIEvent type = UIEvent.Click)
        {
            UI_EventHandler evt = getOrAddEventHandler(go);

            switch (type)
            {
                case UIEvent.Click:
                    evt.OnClickHandler -= action;
                    evt.OnClickHandler += action;
                    break;
                case UIEvent.Drag:
                    evt.OnDragHandler -= action;
                    evt.OnDragHandler += action;
                    break;
            }
        }

        // ─── private 헬퍼들 ─────────────────────────────────────────────
        // Rookiss 의 Util.cs / Extension.cs 를 별도 어셈블리로 두지 않고
        // UI 안에서만 쓰이므로 여기 private static 으로 내장.

        private static UI_EventHandler getOrAddEventHandler(GameObject go)
        {
            UI_EventHandler h = go.GetComponent<UI_EventHandler>();
            if (h == null) h = go.AddComponent<UI_EventHandler>();
            return h;
        }

        private static GameObject findChild(GameObject go, string name, bool recursive)
        {
            Transform t = findChild<Transform>(go, name, recursive);
            return t != null ? t.gameObject : null;
        }

        private static T findChild<T>(GameObject go, string name, bool recursive) where T : UnityEngine.Object
        {
            if (go == null) return null;

            if (recursive)
            {
                // 자식 계층 전체를 재귀 탐색
                foreach (T component in go.GetComponentsInChildren<T>(includeInactive: true))
                {
                    if (string.IsNullOrEmpty(name) || component.name == name)
                        return component;
                }
            }
            else
            {
                // 직속 자식만 탐색
                for (int i = 0; i < go.transform.childCount; i++)
                {
                    Transform child = go.transform.GetChild(i);
                    if (string.IsNullOrEmpty(name) || child.name == name)
                    {
                        T component = child.GetComponent<T>();
                        if (component != null) return component;
                    }
                }
            }

            return null;
        }
    }

    // GameObject 확장 메서드. UI_Base.BindEvent 의 사용성을 위한 syntactic sugar.
    //
    // 사용 예:
    //   go.BindEvent(OnClicked);                          // 클릭
    //   go.BindEvent(OnDragged, UI_Base.UIEvent.Drag);   // 드래그
    public static class UIGameObjectExtensions
    {
        public static void BindEvent(this GameObject go, Action<PointerEventData> action, UI_Base.UIEvent type = UI_Base.UIEvent.Click)
        {
            UI_Base.BindEvent(go, action, type);
        }
    }
}
