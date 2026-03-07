/**
 * Theme System — Phase 7
 *
 * Supports: "dark" | "light" | "system"
 * Persisted via configStore. Applies CSS class on <html>.
 */

export type ThemeMode = "dark" | "light" | "system";

/** Resolve "system" to the actual preference */
function getSystemTheme(): "dark" | "light" {
  if (typeof window === "undefined") return "dark";
  return window.matchMedia("(prefers-color-scheme: dark)").matches
    ? "dark"
    : "light";
}

/** Apply the resolved theme to <html> element */
export function applyTheme(mode: ThemeMode): void {
  const resolved = mode === "system" ? getSystemTheme() : mode;
  const html = document.documentElement;
  html.setAttribute("data-theme", resolved);
  html.classList.toggle("light", resolved === "light");
  html.classList.toggle("dark", resolved === "dark");
}

/** Listen for OS theme changes (only matters when mode === "system") */
export function watchSystemTheme(callback: () => void): () => void {
  const mq = window.matchMedia("(prefers-color-scheme: dark)");
  const handler = () => callback();
  mq.addEventListener("change", handler);
  return () => mq.removeEventListener("change", handler);
}
