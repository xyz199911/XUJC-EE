n = int(input("请输入一个正整数："))
print()
primes = []
for x in range(2, n + 1):
    is_prime = True
    for d in range(2, int(x ** 0.5) + 1):
        if x % d == 0:
            is_prime = False
            break
    if is_prime:
        primes.append(str(x))
print(' '.join(primes))
print(f"共 {len(primes)} 个素数")
