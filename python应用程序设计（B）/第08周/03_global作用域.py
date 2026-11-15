# 第一组：用 global 声明，修改全局变量
g = 10

def change_global():
    global g
    g = 99

change_global()
print("全局修改后 g =", g)

# 第二组：不用 global，函数内赋值是局部变量，不影响全局
g = 10

def local_assign():
    g = 5  # 这是局部变量，和外面的 g 无关

local_assign()
print("局部赋值不影响全局 g =", g)
