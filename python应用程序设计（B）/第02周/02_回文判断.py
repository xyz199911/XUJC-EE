s = input("请输入一句话：")
s_no_space = s.replace(" ","")
if s_no_space == s_no_space[::-1]:
    print("是回文")
else:
    print("不是回文")
