using System;
using TMPro;
using UnityEngine.UI;

namespace Client.UI
{
    // 로그인 화면 UI.
    //
    // 책임:
    //   - 입력 필드, 버튼, 상태 텍스트의 자동 바인딩 (Bind 패턴)
    //   - 버튼 클릭을 이벤트로 외부에 발행
    //   - 외부(컨트롤러)에서 호출할 인터페이스 제공 (GetId, GetPassword, SetStatus 등)
    //
    // 책임이 아닌 것:
    //   - 네트워크 시퀀스 (LoginSceneController 가 함)
    //   - 씬 전환 (LoginSceneController 가 함)
    //
    // 자식 GameObject 이름은 아래 enum 값과 정확히 일치해야 함.
    // 일치하지 않으면 Bind 시 Debug.LogWarning 출력 후 해당 인덱스는 null.
    public class UI_Login : UI_Scene
    {
        // ─── Bind 대상 ────────────────────────────────────────────────
        // enum 의 각 값 이름이 프리팹 내 자식 GameObject 이름과 정확히 매칭되어야 함.

        private enum InputFields
        {
            IdInput,
            PasswordInput,
        }

        private enum Buttons
        {
            LoginButton,
        }

        private enum Texts
        {
            TitleText,
            StatusText,
        }

        // ─── 외부 이벤트 ───────────────────────────────────────────────
        // 컨트롤러가 구독.

        public event Action OnLoginClicked;

        // ─── 초기화 ───────────────────────────────────────────────────

        public override void Init()
        {
            base.Init();

            Bind<TMP_InputField>(typeof(InputFields));
            Bind<Button>(typeof(Buttons));
            Bind<TextMeshProUGUI>(typeof(Texts));

            // 버튼 클릭 → 외부 이벤트 발행.
            // BindEvent 는 PointerClick 이벤트를 잡으므로 일반 GameObject 에도 적용 가능.
            // (Button.onClick 도 가능하지만 Rookiss 패턴 일관성을 위해 BindEvent 사용)
            Get<Button>((int)Buttons.LoginButton).gameObject.BindEvent(_ => OnLoginClicked?.Invoke());
        }

        // ─── 외부 인터페이스 ──────────────────────────────────────────

        public string GetId()
        {
            TMP_InputField field = Get<TMP_InputField>((int)InputFields.IdInput);
            return field != null ? field.text.Trim() : string.Empty;
        }

        public string GetPassword()
        {
            TMP_InputField field = Get<TMP_InputField>((int)InputFields.PasswordInput);
            return field != null ? field.text : string.Empty;
        }

        public void SetStatus(string message)
        {
            TextMeshProUGUI text = Get<TextMeshProUGUI>((int)Texts.StatusText);
            if (text != null) text.text = message;
        }

        public void SetLoginButtonInteractable(bool interactable)
        {
            Button button = Get<Button>((int)Buttons.LoginButton);
            if (button != null) button.interactable = interactable;
        }

        public void SetTitle(string title)
        {
            TextMeshProUGUI text = Get<TextMeshProUGUI>((int)Texts.TitleText);
            if (text != null) text.text = title;
        }
    }
}
