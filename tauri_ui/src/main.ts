import { mount } from "svelte";

// Disable browser context menu globally
document.addEventListener("contextmenu", (e) => e.preventDefault());

async function bootstrap() {
  const target = document.getElementById("app")!;

  let label = "";
  try {
    const { getCurrentWindow } = await import("@tauri-apps/api/window");
    label = getCurrentWindow().label;
  } catch {}

  if (label === "tray_popup") {
    const { default: TrayPopup } = await import("./lib/TrayPopup.svelte");
    mount(TrayPopup, { target });
  } else {
    const { default: App } = await import("./App.svelte");
    mount(App, { target });
  }
}

bootstrap();
