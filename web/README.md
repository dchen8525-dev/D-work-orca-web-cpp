# web/ —— 零构建静态页

P5 会在这里实现原 `apps/web` 的全部界面。**这里不放任何 Node / npm / Vite / React 文件**：
浏览器直接加载 `index.html` + ES modules（`type="module"`）+ `styles.css`。

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

## 计划文件

`index.html`、`styles.css`、`app.js`、`api.js`、`hub.js`、`envelope.js`、`dom.js`、`util.js`、
`views/{auth,board,task,create,automations,markdown}.js`，可选 `manifest.webmanifest` / `icon.svg` / `sw.js`。

## 验收（P5）

390×844 视口下：登录 → 看板 → 任务详情 → 回复 / 新建 / 中止 / 审批，断线重连后自动重订阅并 refetch，
控制台无报错。
