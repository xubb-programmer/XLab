#pragma once

#include <QByteArray>
#include <QDateTime>

enum class TargetKind {
    Reconnaissance,
    BurstPoint
};

QByteArray encode1501(TargetKind kind, quint64 targetNumber, double longitude,
                      double latitude, double altitude, const QDateTime &reportedAt);
