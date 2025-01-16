import matplotlib
matplotlib.use('Agg')  # Устанавливаем бэкенд Agg
import matplotlib.pyplot as plt
from io import BytesIO
import base64
from datetime import datetime

def plot_temperature(data, title):
    """Функция для построения графика температуры."""
    if not data or "data" not in data:
        return None

    timestamps = [datetime.strptime(entry["timestamp"], "%Y-%m-%d %H:%M:%S") for entry in data["data"]]
    values = [entry["value"] for entry in data["data"]]

    plt.figure(figsize=(10, 6))
    plt.plot(timestamps, values, marker='o', linestyle='-', color='b')
    plt.title(title)
    plt.xlabel("Time")
    plt.ylabel("Temperature (°C)")
    plt.ylim(0, 35)  # Ограничиваем ось Y от 0 до 35
    plt.yticks(range(0, 36, 5))  # Деления на оси Y с шагом 5
    plt.grid(True)

    # Сохраняем график в буфер
    buf = BytesIO()
    plt.savefig(buf, format="png")
    plt.close()  # Закрываем график, чтобы освободить память
    buf.seek(0)
    return base64.b64encode(buf.getvalue()).decode("utf-8")