import { Component, ErrorInfo, ReactNode } from "react";

// Last-resort fallback so a render-time exception anywhere in the tree
// shows a readable error instead of a silent white window. Preventing
// future "Settings just opens blank" reports — the v0.1 → v0.2 upgrade
// bug took ages to diagnose precisely because there was nothing on
// screen to inspect.
//
// Kept dictionary-free on purpose: the i18n module itself could be the
// thing that crashed, so the fallback uses static bilingual text and
// must not depend on any other app state.

interface Props {
  children: ReactNode;
}

interface State {
  error: Error | null;
}

export class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: ErrorInfo): void {
    // Log to the devtools console (when available) — useful in dev
    // builds and when users open release-build devtools manually.
    // eslint-disable-next-line no-console
    console.error("Settings UI crashed:", error, info);
  }

  private handleReload = (): void => {
    // Full reload re-runs load_config, which is usually enough to
    // recover from a transient state issue.
    if (typeof window !== "undefined") {
      window.location.reload();
    }
  };

  render(): ReactNode {
    if (this.state.error) {
      const message = this.state.error.message || String(this.state.error);
      const stack = this.state.error.stack ?? "";
      return (
        <div
          className="app"
          role="alert"
          style={{ padding: "1.5rem", maxWidth: "100%" }}
        >
          <h1 style={{ marginTop: 0 }}>
            Settings crashed / 设置界面崩溃
          </h1>
          <p>
            Something went wrong while rendering the form. Please reload, and
            if it keeps happening, share the details below.
          </p>
          <p>
            渲染设置界面时出错。请点击下方按钮重新加载；若问题持续，请把下面的错误信息反馈给我们。
          </p>
          <pre
            style={{
              whiteSpace: "pre-wrap",
              wordBreak: "break-word",
              background: "var(--code-bg, #f4f4f4)",
              padding: "0.75rem",
              borderRadius: 4,
              fontSize: "0.85rem",
            }}
          >
            {message}
            {stack ? "\n\n" + stack : ""}
          </pre>
          <button className="btn btn-primary" onClick={this.handleReload}>
            Reload / 重新加载
          </button>
        </div>
      );
    }
    return this.props.children;
  }
}
