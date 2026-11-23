def analizar_senales_multiples(s1, s2, s3, s4, s5, s6, s7, s8):

    a = s1 + s2
    b = s3 + s4
    c = s5 + s6
    d = s7 + s8

    e = a * b
    f = c * d

    g = s1 - s3
    h = s2 - s4
    i = s5 - s7
    j = s6 - s8

    k = g + h
    l = i + j

    m = a * s5
    n = b * s6
    o = c * s7
    p = d * s8

    q = m + n
    r = o + p

    t = s1 * s8
    u = s2 * s7
    w = s3 * s6
    x = s4 * s5

    resultado = (e + f + k + l + m + n + o + p + q + r + t + u + w + x +
                 a + b + c + d + g + h + i + j)

    return resultado
