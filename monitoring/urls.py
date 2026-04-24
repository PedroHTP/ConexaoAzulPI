from django.urls import path

from .views import dashboard, healthcheck, receive_sensor_data

urlpatterns = [
    path("", dashboard, name="dashboard"),
    path("api/health/", healthcheck, name="healthcheck"),
    path("api/readings/", receive_sensor_data, name="receive_sensor_data"),
]
