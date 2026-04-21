import { Server } from '@modelcontextprotocol/sdk/server/index.js'
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js'
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from '@modelcontextprotocol/sdk/types.js'
import { z } from 'zod'
import { bus } from './bus.js'
import { state } from './state.js'
import { logger } from './logger.js'

const PERMISSION_REQUEST_METHOD = 'notifications/claude/channel/permission_request'
const PERMISSION_VERDICT_METHOD = 'notifications/claude/channel/permission'
const CHANNEL_MESSAGE_METHOD = 'notifications/claude/channel'

const PermissionRequestNotification = z.object({
  method: z.literal(PERMISSION_REQUEST_METHOD),
  params: z.object({
    request_id: z.string(),
    tool_name: z.string(),
    description: z.string(),
    input_preview: z.string(),
  }),
})

const ReplyToolInput = z.object({
  chat_id: z.string(),
  text: z.string(),
})

const REPLY_TOOL = {
  name: 'reply',
  description: 'Send a message back to the remote client through the bridge channel.',
  inputSchema: {
    type: 'object' as const,
    properties: {
      chat_id: {
        type: 'string',
        description: 'The chat_id from the inbound <channel> tag. Pass back exactly as received.',
      },
      text: { type: 'string', description: 'Message text to send back to the remote client.' },
    },
    required: ['chat_id', 'text'],
  },
}

export async function startMcpServer(version: string): Promise<void> {
  const server = new Server(
    { name: 'claude-code-bridge', version },
    {
      capabilities: {
        experimental: {
          'claude/channel': {},
          'claude/channel/permission': {},
        },
        tools: {},
      },
      instructions:
        'Messages from the bridge channel arrive as <channel source="bridge" chat_id="..."> tags. ' +
        'Each message is from a remote client (HTTP or WebSocket) connected to the bridge. ' +
        'To reply to a remote client, call the `reply` tool and pass back the exact `chat_id` ' +
        'from the incoming tag. Treat these messages as if the user had typed them directly.',
    },
  )

  server.setRequestHandler(ListToolsRequestSchema, async () => ({
    tools: [REPLY_TOOL],
  }))

  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    if (request.params.name !== 'reply') {
      throw new Error(`Unknown tool: ${request.params.name}`)
    }
    const parsed = ReplyToolInput.safeParse(request.params.arguments ?? {})
    if (!parsed.success) {
      throw new Error(`Invalid reply arguments: ${parsed.error.message}`)
    }
    const { chat_id, text } = parsed.data
    state.appendMessage(chat_id, { direction: 'outbound', content: text, ts: Date.now() })
    bus.emit('outbound_reply', { chat_id, text })
    logger.info(`reply chat_id=${chat_id} len=${text.length}`)
    return { content: [{ type: 'text', text: 'sent' }] }
  })

  server.setNotificationHandler(PermissionRequestNotification, async ({ params }) => {
    const entry = state.addPendingPermission(params)
    bus.emit('permission_request', entry)
    logger.info(`permission_request id=${params.request_id} tool=${params.tool_name}`)
  })

  bus.on('inbound_message', (payload) => {
    const meta: Record<string, string> = {
      chat_id: payload.chat_id,
      client_id: payload.client_id,
      source: 'bridge',
      ...(payload.meta ?? {}),
    }
    server
      .notification({
        method: CHANNEL_MESSAGE_METHOD,
        params: { content: payload.content, meta },
      })
      .catch((err) => logger.error(`channel notification failed: ${String(err)}`))
  })

  bus.on('permission_verdict', (payload) => {
    const pending = state.resolvePendingPermission(payload.request_id)
    const verdictNotification = {
      method: PERMISSION_VERDICT_METHOD,
      params: { request_id: payload.request_id, behavior: payload.behavior },
    }
    logger.info(`verdict about to send: ${JSON.stringify(verdictNotification)}`)
    server
      .notification(verdictNotification)
      .then(() => {
        bus.emit('permission_resolved', {
          request_id: payload.request_id,
          behavior: payload.behavior,
          by: 'remote',
        })
        logger.info(
          `verdict sent id=${payload.request_id} behavior=${payload.behavior} ` +
            `pending_was=${pending ? 'yes' : 'no'}`,
        )
      })
      .catch((err) => logger.error(`verdict notification failed: ${String(err)}`))
  })

  const transport = new StdioServerTransport()
  await server.connect(transport)
  logger.info('mcp stdio transport connected — waiting for Claude Code')
}
