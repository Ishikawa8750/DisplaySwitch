/**
 * Ambient Light Sensor (ALS) — Phase 7
 *
 * Reads ambient light from the Rust backend (PowerShell Windows.Devices.Sensors bridge).
 * Provides lux → brightness mapping with logarithmic curve.
 */

export interface ALSStatus {
  available: boolean;
  lux: number;
  recommended_brightness: number;
}

let _available: boolean | null = null;

/**
 * Query the ambient light sensor via Tauri invoke.
 * Returns { available, lux, recommended_brightness }.
 */
export async function getAmbientLight(): Promise<ALSStatus> {
  try {
    const { invoke } = await import("@tauri-apps/api/core");
    const result = await invoke<ALSStatus>("get_ambient_light");
    _available = result.available;
    return result;
  } catch {
    return { available: false, lux: -1, recommended_brightness: -1 };
  }
}

/** Check if ALS was found on last query */
export function isALSAvailable(): boolean {
  return _available === true;
}

/**
 * Convert lux to recommended brightness (0-100).
 * Logarithmic curve matching human perception.
 *
 *   0-10 lux (dark room)    → 10-25%
 *   10-100 lux (dim room)   → 25-50%
 *   100-500 lux (office)    → 50-75%
 *   500-10000 lux (daylight) → 75-100%
 */
export function luxToBrightness(lux: number): number {
  if (lux <= 0) return 10;
  const logLux = Math.log10(Math.max(lux, 1)); // 0 to ~4
  const brightness = Math.round(10 + (logLux / 4.0) * 90);
  return Math.max(10, Math.min(100, brightness));
}

/**
 * Start auto-brightness: periodically reads ALS and applies brightness.
 * Returns a cleanup function.
 */
export function startAutoBrightness(
  intervalMs: number,
  applyFn: (brightness: number) => Promise<void>,
): () => void {
  let active = true;

  const loop = async () => {
    while (active) {
      try {
        const als = await getAmbientLight();
        if (als.available && als.lux >= 0) {
          const target = luxToBrightness(als.lux);
          await applyFn(target);
        }
      } catch {
        // ignore
      }
      await new Promise((r) => setTimeout(r, intervalMs));
    }
  };

  loop();
  return () => { active = false; };
}
