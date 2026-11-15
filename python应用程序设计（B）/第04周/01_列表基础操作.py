scores = [78, 88, 95, 66]
print(scores[0], scores[-1], len(scores))

scores[1] = 99
scores.append(100)
print(scores)

scores.insert(0, 50)
scores.remove(95)
print(scores)
