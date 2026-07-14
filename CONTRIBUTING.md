 # Contributing to XUJC-EE

 感谢你愿意参与这个学习资源共享计划！以下是一些简单的协作规范。

 ## 提交课程资料

 如果你想新增一门课的复习资料，请按以下步骤：

 1. Fork 本仓库
 2. 创建新的分支：`git checkout -b add-course-xxx`
 3. 添加你的课程目录，结构参考如下：
    ```
    课程名称/
    ├── README.md        # 课程介绍 + 📌 重点看这里表格
    ├── 课件/             # 课程PPT/PDF
    └── 笔记/             # 你的复习笔记
    ```
 4. 在根目录 [README.md](./README.md) 的课程目录表格中补充你的课程
 5. Commit 时遵循常规格式：`feat: add xxx course materials`
 6. 发起 Pull Request

## Commit 规范

为了让历史整洁可读，建议按以下标签组织 commit 信息：

| 标签 | 用途 |
|------|------|
| `feat` | 新增课程或资料 |
| `fix` | 修正错误链接或文件 |
| `docs` | 更新文档/README |
| `chore` | 配置、CI、工具链相关 |
| `refactor` | 重组目录结构 |

示例：
- `feat: add 数字逻辑电路复习笔记`
- `fix: correct broken link in 信号与系统 README`
- `docs: update 人工智能 README with exam strategy`
- `chore: add GitHub Actions markdown lint`

## 提问与建议

开 Issue 就好，我看到会回复。

## License

贡献的资料同样遵循 MIT License。
