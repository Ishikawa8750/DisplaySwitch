/**
 * Toast Notification System — Phase 7
 *
 * Lightweight reactive toast store for in-app notifications.
 * Shows brief feedback for hotkey actions, preset switches, errors, etc.
 */

export type ToastType = "info" | "success" | "warning" | "error";

export interface ToastMessage {
  id: number;
  text: string;
  type: ToastType;
  timeout: number;
}

let _nextId = 1;
let _toasts: ToastMessage[] = $state([]);
let _listeners: Array<() => void> = [];

function notify() {
  for (const fn of _listeners) fn();
}

export function getToasts(): ToastMessage[] {
  return _toasts;
}

export function onToastsChange(fn: () => void): () => void {
  _listeners.push(fn);
  return () => {
    _listeners = _listeners.filter((f) => f !== fn);
  };
}

export function addToast(
  text: string,
  type: ToastType = "info",
  timeout = 2500,
): number {
  const id = _nextId++;
  _toasts = [..._toasts, { id, text, type, timeout }];
  notify();
  // Auto-dismiss
  setTimeout(() => dismissToast(id), timeout);
  return id;
}

export function dismissToast(id: number): void {
  _toasts = _toasts.filter((t) => t.id !== id);
  notify();
}

/** Convenience shortcuts */
export const toast = {
  info: (text: string, timeout?: number) => addToast(text, "info", timeout),
  success: (text: string, timeout?: number) => addToast(text, "success", timeout),
  warn: (text: string, timeout?: number) => addToast(text, "warning", timeout),
  error: (text: string, timeout?: number) => addToast(text, "error", timeout ?? 4000),
};
