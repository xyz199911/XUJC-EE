lr, epochs, opt, dev = input("请输入 学习率 轮数 优化器 设备（空格分隔）：").split()

config = {
    "learning_rate": float(lr),
    "epochs": int(epochs),
    "optimizer": opt,
    "device": dev,
}

print()
print("学习率：", config["learning_rate"])
print("轮数：", config["epochs"])
print("优化器：", config["optimizer"])
print("设备：", config["device"])
print("调度器：", config.get("scheduler", "未配置"))
