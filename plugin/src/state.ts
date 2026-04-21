import type { ChatMessage, PendingPermission } from './types.js'

const chats = new Map<string, ChatMessage[]>()
const pendingPermissions = new Map<string, PendingPermission>()
const startedAt = Date.now()

export const state = {
  startedAt,

  appendMessage(chat_id: string, msg: ChatMessage): void {
    const log = chats.get(chat_id) ?? []
    log.push(msg)
    chats.set(chat_id, log)
  },

  getChat(chat_id: string): ChatMessage[] | undefined {
    return chats.get(chat_id)
  },

  listChats(): { chat_id: string; message_count: number; last_ts: number }[] {
    return [...chats.entries()].map(([chat_id, msgs]) => ({
      chat_id,
      message_count: msgs.length,
      last_ts: msgs[msgs.length - 1]?.ts ?? 0,
    }))
  },

  addPendingPermission(p: Omit<PendingPermission, 'opened_at'>): PendingPermission {
    const entry: PendingPermission = { ...p, opened_at: Date.now() }
    pendingPermissions.set(p.request_id, entry)
    return entry
  },

  resolvePendingPermission(request_id: string): PendingPermission | undefined {
    const entry = pendingPermissions.get(request_id)
    pendingPermissions.delete(request_id)
    return entry
  },

  getPendingPermission(request_id: string): PendingPermission | undefined {
    return pendingPermissions.get(request_id)
  },

  listPendingPermissions(): PendingPermission[] {
    return [...pendingPermissions.values()]
  },

  uptimeSeconds(): number {
    return Math.floor((Date.now() - startedAt) / 1000)
  },
}
