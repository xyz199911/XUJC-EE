# 基础任务 1.1：字符串方法清理文本
dirty = input("请输入一段需要清理的文本：")
print(f"原始内容：{repr(dirty)}")

clean = dirty.strip()                # 去首尾空白
clean = clean.replace(",", "").replace("!", "").replace(".", "")  # 去常见标点
clean = " ".join(clean.split())      # 连续多空格压缩成一个
print(f"清理后：{repr(clean)}")