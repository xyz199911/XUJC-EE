user = {"name": "Alice", "age": 20}

print(user["name"])
user["age"] = 21
user["city"] = "Xiamen"
print(user)
print(list(user.keys()))
print(list(user.values()))
print(list(user.items()))
print(user.get("email", "无邮箱"))
