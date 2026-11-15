# 进阶任务 2.1：规范化清洗英文
messy = input("请输入一段大小写混乱的英文：")
print(f"原始：{repr(messy)}")

words = messy.strip().lower().split()
clean_words = [w.capitalize() for w in words]
result = " ".join(clean_words)
print(f"清洗后：{result}")