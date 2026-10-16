# Ajuste vigente: entrada alternativa, referencia de 298 mA

El usuario confirmó que la prueba previa de 299 mA correspondía a la entrada nominal de 25 mΩ. Al cambiar a la entrada alternativa, se tomaron 25 muestras de 9.270 mV con referencia de 298 mA. Resistencia equivalente de calibración: 31.107383 mΩ. El sketch usa 0.031107383 Ω tanto para corriente como para umbral de alarma. Para la entrada nominal usar 0.025 Ω. El valor es un ajuste en un punto que incluye posibles errores del INA236 y del instrumento de referencia, pendiente de comprobar a otras corrientes y temperaturas.

Datos: CALIBRACION_ENTRADA_ALTERNATIVA_298mA.json. Los apartados siguientes son históricos.

---

# Ajuste más reciente: referencia de 299 mA

La entrada conectada durante esta prueba dio 25 muestras entre 7.5125 y 7.5150 mV, promedio 7.5137 mV. Con referencia de 0.299 A, R equivalente = 0.025129431 Ω (25.129431 mΩ). El sketch utiliza 0.025129431 Ω para corriente y umbral de alarma. Este valor sustituye al ajuste de prueba de 31.45 mΩ para la entrada actualmente conectada. Es un ajuste en un solo punto que incorpora posibles errores del sensor y de la referencia; no es una medida independiente de resistencia física. Se aplica a todos los sensores del sketch.

Datos guardados en CALIBRACION_299mA.json. Los apartados siguientes son históricos.

---

# Ajuste de prueba para la entrada con desviación de +25.8 %

Se configuró una resistencia efectiva de 31.45 mΩ (25 mΩ × 1.258). Es una compensación experimental de la ruta seleccionada, pendiente de validar en banco; se aplica a todos los sensores configurados por este sketch. Para una entrada que mida solo el shunt nominal debe usarse 0.025 Ω.

La calibración se recalcula a 1628 y la conversión utiliza el LSB efectivo tras redondear. El umbral de alarma de 500 mA se recalcula a 6290 cuentas (15.725 mV). El rango del shunt equivale aproximadamente a ±2.605 A para esta resistencia efectiva. La lectura de voltaje del shunt permanece sin compensar.

Con la lectura anterior de 12.5775 mV, la corriente corregida esperada es aproximadamente 399.9 mA. Probar con varias corrientes y temperaturas antes de considerar validado el factor. Compensar esta ruta no demuestra que la resistencia física del shunt sea 31.45 mΩ.

---

Los datos siguientes describen la revisión original con resistencia nominal, anterior al ajuste de prueba:

# Revisión INA236 con shunt de 25 mΩ

La escala original de corriente era correcta: CAL = 0.00512 / (0.0001 × 0.025) = 2048. Configuración 0x4527: rango ±81.92 mV, promedio de 16 muestras, 1100 µs por canal y conversión continua de bus/shunt. El ciclo nominal completo es 35.2 ms.

Cambios: cálculo automático de CAL y del LSB efectivo, redondeo del umbral de alarma, visualización de AFF junto a GPIO27 y eliminación de las lecturas de potencia. Se mantiene I=Vshunt/R como diagnóstico: no es una referencia independiente, pues usa el mismo ADC. En modo continuo ambos registros pueden pertenecer a ciclos distintos si la carga varía durante la lectura.

## Comprobación en banco

1. Abrir el monitor serie a 115200 baudios. Comparar con una carga DC estable, esperando que se estabilice cada cambio.
2. A 0.500 A, el shunt nominal debe tener 12.500 mV. CAL=2048, registro de shunt=5000 y registro de corriente=5000. La alarma SOL se activa al superar el umbral, no necesariamente al igualarlo.
3. Medir con multímetro los mV directamente entre los puntos de toma IN+ e IN−. Comparar ese valor con Shunt voltage. Comparar también la corriente con un instrumento de referencia en serie.
4. Si el voltaje coincide pero la corriente difiere de la referencia, calcular R efectiva = Vshunt / Ireferencia. Una tolerancia del shunt del 1% puede producir aproximadamente 1% de error de corriente. Revisar pistas, soldaduras, cables y conexiones Kelvin; no ajustar la resistencia por una sola lectura inestable.
5. Si ambas corrientes del programa coinciden pero difieren de la referencia, esa coincidencia no demuestra exactitud: ambas dependen de la misma medida de shunt y la misma resistencia configurada.
6. Revisar el cero sin corriente. Un error aproximadamente constante sugiere offset; un error proporcional a la corriente sugiere resistencia/ganancia. El offset máximo de 5 µV equivale a 0.2 mA con 25 mΩ bajo las condiciones especificadas por TI.

## Límites y alarma

- Rango nominal aproximado: ±3.277 A. El ADC de shunt puede saturarse sin que Math overflow lo indique; este indicador detecta desbordamiento aritmético, no garantiza ausencia de saturación del ADC.
- Alarma configurada solo en 0x40 para corriente positiva mayor que 0.500 A. Las otras direcciones no tienen alarma habilitada. SOL no detecta sobrecorriente negativa.
- ALERT es drenador abierto: conectar a GPIO27 con pull-up externo de 4.7 kΩ a 3.3 V y tierra común.
- El comparador de alarma usa conversiones individuales; la lectura mostrada está promediada. Puede aparecer una alarma por un pico aunque la corriente impresa sea menor que 0.500 A. AFF y GPIO se leen en instantes distintos y pueden diferir durante transitorios.
- El bus se mide en IN− respecto a GND. En una conexión de shunt en el retorno no representa necesariamente el voltaje de alimentación de la carga.

Fuente: https://www.ti.com/lit/ds/symlink/ina236.pdf (secciones 7.6 y 8.1).
