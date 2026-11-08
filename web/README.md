# web/ —— 零构建静态页

P5 已完成：原 `apps/web` 的全部界面都在这里，**没有 Node / npm / Vite / React**。
浏览器直接加载 `index.html` + ES modules（`type="module"`）+ `styles.css`。
把 hub 的 `staticDir` 指向这个目录（或发布时复制到部署目录）即可。

## 硬约束

1. 服务端下发的 CSP 是
   `default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; ...`，
   `script-src` 回落到 `default-src 'self'`，**禁止内联 `<script>` 和 `onclick=` 属性**。
   所有 JS 必须是同源外部文件，事件一律 `addEventListener` / 事件委托。
2. `crypto.subtle.digest('SHA-256')` 与 `crypto.randomUUID()` 用于 mutation envelope，
   **必须是 HTTPS 或 localhost**；纯 HTTP 局域网 IP 下所有写操作都会失败。
3. 原 `styles.css`（约 1783 行标准 CSS）可直接复用，但 DOM 的 class 名 / 层级必须保持一致，
   否则样式失配。
4. 原 `sw.js` 的 `/assets/*` cache-first forever 是为 Vite 内容哈希文件名设计的；
   零构建没有哈希文件名，保留 SW 必须改成 network-first，或干脆省略 SW 只留 manifest。
5. markdown 渲染不能用 `innerHTML` 拼 agent 输出，必须用 DOM API 逐节点创建或手动转义。

## 文件

| 文件 | 作用 |
|---|---|
| `index.html` | 唯一 HTML：manifest / icon / styles.css / `app.js` |
| `app.js` | 会话检查、登录/退出、路由（pushState/popstate + 深链返回）、HubClient 生命周期 |
| `hub.js` | `/client` WebSocket：退避重连、ping/静默检测、重连后重订阅并发 `reset`、cookie 失效判定 |
| `api.js` / `envelope.js` | 同源 JSON API；canonical-JSON + SHA-256 mutation envelope |
| `dom.js` / `util.js` | DOM 构造（只走 Text node / textContent）与格式化 |
| `views/board.js` | 看板：项目→工作区→任务、搜索/筛选/折叠、15s 刷新、提醒 |
| `views/task.js` | 任务：历史分页、350ms 去抖 refetch、回复/停止/审批/回答/setOption |
| `views/create.js` | 新建结构化 agent 会话（先探测 `agentSession.createSupport`） |
| `views/automations.js` | 定时任务列表 + 编辑器（预设/cron 生成、工作区选择、运行记录） |
| `views/auth.js` / `views/markdown.js` | 登录/改密；依赖零的 markdown 子集 |
| `styles.css` | 复用原样式；唯一新增是末尾的 `[hidden] { display: none !important }` |

没有 `sw.js`：原始 service worker 的 cache-first 依赖 Vite 的内容哈希文件名，
零构建下没有哈希，按硬约束 4 直接省略（manifest 仍可安装到主屏）。

## 实现要点

- 状态帧可能很密（每个工具事件一次）。看板与任务页都按 key 复用 DOM 节点，
  否则一次重绘落在 pointerdown/pointerup 之间就会吞掉点击；列表内容再按签名去重。
- 任务页有一个 350ms 去抖的 refetch；正在输入的「其他答案」会推迟重绘到失焦之后。
- 所有 agent 输出用 DOM API 渲染，未使用 `innerHTML`；外部链接带
  `target="_blank" rel="noreferrer noopener"`。

## 本地联调

1. `orca-server --init-config <临时>/server.json`，把 `staticDir` 改成
   `D:/work/orca-cpp/web`（绝对路径），`sessionStorePath` 指向临时目录。
2. `orca-gateway --config <临时>/gateway.json --local-orca`；`serverUrl` 必须是
   `ws://127.0.0.1:8788/gateway`（读取配置时不会自动补 `/gateway`）。
3. 手机视口用 DevTools 的 `emulate`（`390x844x3,mobile,touch`），
   窗口 resize 会被最小宽度钳住。

## 验收（P5，已完成）

390×844 视口下：登录 → 看板 → 任务详情 → 回复 / 新建 / 中止 / 审批，断线重连后自动重订阅并 refetch，
控制台无报错。实际跑过的清单见 `HANDOFF.md` §2.6.3。
