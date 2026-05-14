/** HTTP bridge to robot raw API (ported from simulator_v3.html). */

export const BRIDGE_TOKEN_KEY = "claude-code-wrapper.bridge_ws_dev_token";
export const BRIDGE_HTTP_BASE_KEY = "claude-code-wrapper.bridge_http_base";

export function normalizeBridgeBase(raw: string | null): string {
  const s = (raw || "").trim().replace(/\/+$/, "");
  return s || "http://localhost:8787";
}

export function bridgeBase(): string {
  if (typeof window === "undefined") return "http://localhost:8787";
  return normalizeBridgeBase(localStorage.getItem(BRIDGE_HTTP_BASE_KEY));
}

export function bridgeToken(): string {
  if (typeof window === "undefined") return "";
  return (localStorage.getItem(BRIDGE_TOKEN_KEY) || "").trim();
}

export async function postRaw(
  path: string,
  body: unknown,
): Promise<Response | null> {
  const url = bridgeBase() + path;
  const token = bridgeToken();
  try {
    const headers: Record<string, string> = {
      "Content-Type": "application/json",
    };
    if (token) headers.Authorization = "Bearer " + token;
    const res = await fetch(url, {
      method: "POST",
      headers,
      body: JSON.stringify(body ?? {}),
    });
    if (!res.ok) {
      const text = await res.text().catch(() => "");
      console.warn("raw POST failed", res.status, res.statusText, url, text);
    }
    return res;
  } catch (e) {
    console.warn("raw POST error", url, e);
    return null;
  }
}
