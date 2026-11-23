def procesar_bloques(bloques):

    total = 0

    for bloque in bloques:

        i = 0

        while i < len(bloque):

            total += bloque[i] * 2

            i += 1

    return total