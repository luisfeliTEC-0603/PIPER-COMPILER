def calcular_promedio_ventas(ventas):

    total = 0
    contador = 0

    for v in ventas:
        total += v
        contador += 1

    if contador == 0:
        return 0

    promedio = total / contador

    return promedio


def calcular_promedio_temperaturas(temperaturas):

    suma = 0
    cantidad = 0

    for t in temperaturas:
        suma += t
        cantidad += 1

    if cantidad == 0:
        return 0

    media = suma / cantidad

    return media


def calcular_promedio_gastos(gastos):

    total = 0
    contador = 0

    for v in gastos:
        total += v
        contador += 1

    if contador == 0:
        return 0

    promedio = total / contador

    return promedio


def calcular_mediana_edades(edades):

    datos = sorted(edades)
    n = len(datos)

    if n == 0:
        return 0

    medio = n // 2

    if n % 2 == 0:
        return (datos[medio - 1] + datos[medio]) / 2

    return datos[medio]
