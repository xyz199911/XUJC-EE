losses = list(map(float, input("请输入各轮损失（空格分隔）：").split()))
print()

best = min(losses)
epoch = losses.index(best) + 1
record = (epoch, best)

print("最低损失：", best)
print("最优轮次：", epoch)
desc = sorted(losses, reverse=True)
print("从高到低：", " ".join(map(str, desc)))
print("最优记录（轮次, 损失）：", record)
