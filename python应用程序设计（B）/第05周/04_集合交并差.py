userA = {"电子", "游戏", "运动"}
userB = {"运动", "摄影", "电子"}

jiaji = sorted(userA & userB)
bingji = sorted(userA | userB)
chaji = sorted(userA - userB)

print("共同兴趣：", jiaji, "，共", len(jiaji), "个")
print("全部兴趣：", bingji, "，共", len(bingji), "个")
print("用户A独有：", chaji, "，共", len(chaji), "个")
