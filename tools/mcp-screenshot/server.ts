import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { readFileSync, mkdirSync, existsSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";
import { z } from "zod";

const __dirname = dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = join(__dirname, "../..");
const SCREENSHOT_DIR = join(PROJECT_ROOT, "screenshots");
const DEBUG_PORT = 9222;

if (!existsSync(SCREENSHOT_DIR)) {
  mkdirSync(SCREENSHOT_DIR, { recursive: true });
}

interface BrowserResponse {
  ok?: boolean;
  error?: string;
  path?: string;
  width?: number;
  height?: number;
}

async function sendBrowserCommand(command: Record<string, unknown>): Promise<BrowserResponse> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://127.0.0.1:${DEBUG_PORT}`);
    const timeout = setTimeout(() => {
      ws.close();
      reject(new Error("Timeout waiting for browser response"));
    }, 10000);

    ws.addEventListener("open", () => {
      ws.send(JSON.stringify(command));
    });

    ws.addEventListener("message", (ev: MessageEvent) => {
      clearTimeout(timeout);
      resolve(JSON.parse(String(ev.data)));
      ws.close();
    });

    ws.addEventListener("error", () => {
      clearTimeout(timeout);
      reject(new Error("Cannot connect to browser. Is it running with 'make dev'?"));
    });
  });
}

const server = new McpServer({
  name: "explorer-screenshot",
  version: "1.0.0",
});

server.tool(
  "screenshot",
  "Takes a screenshot of the Explorer browser window and returns it as an image. Browser must be running with 'make dev'.",
  {},
  async () => {
    try {
      const resp = await sendBrowserCommand({
        command: "screenshot",
        path: SCREENSHOT_DIR,
      });

      if (resp.error) {
        return {
          content: [{ type: "text" as const, text: `Screenshot failed: ${resp.error}` }],
          isError: true,
        };
      }

      const imgData = readFileSync(resp.path!);
      return {
        content: [
          {
            type: "image" as const,
            data: imgData.toString("base64"),
            mimeType: "image/png",
          },
          {
            type: "text" as const,
            text: `${resp.path} (${resp.width}x${resp.height})`,
          },
        ],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        content: [{ type: "text" as const, text: `Error: ${message}` }],
        isError: true,
      };
    }
  }
);

server.tool(
  "navigate",
  "Navigates the Explorer browser to a given URL. Browser must be running with 'make dev'.",
  {
    url: z.string().describe("The URL to navigate to"),
  },
  async ({ url }) => {
    try {
      const resp = await sendBrowserCommand({
        command: "navigate",
        url,
      });

      if (resp.error) {
        return {
          content: [{ type: "text" as const, text: `Navigation failed: ${resp.error}` }],
          isError: true,
        };
      }

      return {
        content: [
          {
            type: "text" as const,
            text: `Navigated to: ${(resp as any).url ?? url}`,
          },
        ],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        content: [{ type: "text" as const, text: `Error: ${message}` }],
        isError: true,
      };
    }
  }
);

const transport = new StdioServerTransport();
await server.connect(transport);
