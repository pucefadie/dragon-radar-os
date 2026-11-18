# Project AI Watch — activation and operations

This file documents the installed automation; adding it triggers a safe first-run check via a main-branch push.

- Workflow: [Project AI Watch](../workflows/project-ai-watch.yml)
- Watched branch: `feat/espnow-m1-bidirectional`
- Schedule: approximately every ten minutes (minute 07,17,27,37,47,57 of every hour, UTC). GitHub schedules are best effort.
- Monitoring register: https://github.com/pucefadie/dragon-radar-os/issues/3
- AI review requires repository secret `OPENAI_API_KEY`; it is intentionally never printed or committed.
- Optional GitHub Actions repository variable: `AI_REVIEW_MODEL` (default `gpt-5-mini`).
- The Action performs a read-only GitHub review and posts comments in its dedicated issue when the monitored commit SHA changes. It never flashes ESP32, changes OTA/partitions, merges PRs, or starts Codex.
- Subscribe to the issue for notifications, and consult Actions run logs for API errors or permissions problems.
- Disable via Actions > Project AI Watch > Disable workflow, or remove the cron/push triggers from the YAML.
- API usage is separately billed by OpenAI; GitHub Actions compute usage may also incur charges. No AI call should occur when the watched SHA is unchanged.
