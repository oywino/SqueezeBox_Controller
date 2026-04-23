## Operational model checklist for long-running AI-assisted software development

1. **Define one “source of truth” progress file.**  
   Maintain a single short progress/state document (e.g., `PROJECT_STATE.md`) that always contains: current goal, current status, exact next task, and the acceptance test for that task. [Anthropic+1](https://www.anthropic.com/engineering/effective-harnesses-for-long-running-agents?utm_source=chatgpt.com)

2. **Scope work as small, reviewable tasks.**  
   Each request to the AI must be a narrowly-scoped “issue”: one objective, explicit constraints, and a clear done-definition. [GitHub Docs+1](https://docs.github.com/copilot/how-tos/agents/copilot-coding-agent/best-practices-for-using-copilot-to-work-on-tasks?utm_source=chatgpt.com)

3. **Use a “context pack” instead of pasting history.**  
   For every new chat (or when context degrades), provide only:
- the progress file (item 1)

- the *few* relevant files (or a diff/patch)

- the last failing log section (trimmed)  
  This is the practical version of “context engineering”: keep only the tokens that matter for the current task. [Anthropic+1](https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents?utm_source=chatgpt.com)
4. **Order long inputs correctly.**  
   When you must include long text: put the long documents first, and put the actual question/request at the end. [Claude](https://platform.claude.com/docs/en/build-with-claude/prompt-engineering/long-context-tips?utm_source=chatgpt.com)

5. **Run a fixed “get bearings” preflight at the start of each task.**  
   AI (and you) should always begin by: checking working directory/repo state, reading the progress file, then reading only the files needed for the task. [Anthropic](https://www.anthropic.com/engineering/effective-harnesses-for-long-running-agents?utm_source=chatgpt.com)

6. **Explore the codebase by “pointing,” not dumping.**  
   Ask/answer questions using directory targets, specific files, specific symbols, or specific line ranges—avoid sending entire trees or whole-file dumps unless required. [GitHub Docs+1](https://docs.github.com/en/copilot/tutorials/explore-a-codebase?utm_source=chatgpt.com)

7. **Actively prune context between tasks.**  
   When a task is complete, reset the working context for the next task (practically: start a new chat or re-send only the context pack). This mirrors the documented practice of clearing context to prevent drift and degradation. [Anthropic+1](https://www.anthropic.com/engineering/claude-code-best-practices?utm_source=chatgpt.com)

8. **Require verification gates (“check the work”).**  
   Every change must end with: compile/build + the specific acceptance test defined in item 1, plus a brief sanity check (what changed, why it’s correct). [GitHub Docs+1](https://docs.github.com/en/copilot/get-started/best-practices?utm_source=chatgpt.com)

9. **Prefer diffs/patches for large changes.**  
   Exchange changes as patches (or a small set of files), not as “here are 2,000 lines,” to keep context stable and reviewable. [Anthropic+1](https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents?utm_source=chatgpt.com)

10. **Keep an always-accurate “next task” queue.**  
    Maintain a small prioritized list of remaining tasks and always pick the top unfinished one—don’t rely on chat history to remember priorities. [Anthropic+1](https://www.anthropic.com/engineering/effective-harnesses-for-long-running-agents?utm_source=chatgpt.com)
