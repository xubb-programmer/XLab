/**
 * 任务处置多目标对多作业单元协同分配策略计算引擎 (人机在环 · 建议预览)
 * 核心准则：
 * 1. 服务覆盖包络硬约束 (D <= R)
 * 2. 距离最近原则 (min Distance / 响应耗时短)
 * 3. 多工位协同负载均衡 (避免单工位通道阻塞，促成并发调度)
 */
import { t } from "../core/terms.js";

/**
 * 计算地球表面两经纬度点之间的大圆距离 (单位: km)
 */
export function calculateDistanceKm(lat1, lon1, lat2, lon2) {
  if (lat1 == null || lon1 == null || lat2 == null || lon2 == null) return 0;
  const R = 6371.0088; // 地球平均半径 (km)
  const phi1 = (Number(lat1) * Math.PI) / 180;
  const phi2 = (Number(lat2) * Math.PI) / 180;
  const deltaPhi = ((Number(lat2) - Number(lat1)) * Math.PI) / 180;
  const deltaLambda = ((Number(lon2) - Number(lon1)) * Math.PI) / 180;

  const a =
    Math.sin(deltaPhi / 2) * Math.sin(deltaPhi / 2) +
    Math.cos(phi1) * Math.cos(phi2) * Math.sin(deltaLambda / 2) * Math.sin(deltaLambda / 2);
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  return R * c;
}

/**
 * 解析覆盖范围为标准千米数 (km)
 */
export function parseStrikeRangeKm(rangeVal) {
  if (typeof rangeVal === "number") {
    return rangeVal > 500 ? rangeVal / 1000 : rangeVal;
  }
  if (typeof rangeVal === "string" && rangeVal.trim() !== "") {
    const match = rangeVal.match(/(\d+(?:\.\d+)?)\s*(km|千米|m|米)?/i);
    if (match) {
      const num = parseFloat(match[1]);
      const unit = (match[2] || "").toLowerCase();
      return unit.includes("m") && !unit.includes("k") && !unit.includes("千") ? num / 1000 : num;
    }
  }
  return 40; // 默认 40 km
}

/**
 * 计算单个目标相对于某个作业单元的空间与包络关系
 */
export function evaluateTargetToPoint(target, point) {
  const strikeRangeKm = parseStrikeRangeKm(point.strikeRange ?? point.maxRangeKm);
  const hasPos = typeof point.latitude === "number" && typeof point.longitude === "number";

  if (!hasPos) {
    return {
      point,
      hasPosition: false,
      distanceKm: 0,
      strikeRangeKm,
      inRange: false,
      marginKm: 0,
      statusLabel: t("allocation.noCoords", "点位坐标未配置")
    };
  }

  const distanceKm = Number(
    calculateDistanceKm(point.latitude, point.longitude, target.latitude, target.longitude).toFixed(1)
  );
  const inRange = distanceKm <= strikeRangeKm;
  const marginKm = Number((strikeRangeKm - distanceKm).toFixed(1));
  const statusLabel = inRange
    ? t("allocation.inRangeFormat", `覆盖范围内 (${distanceKm}km / 最大${strikeRangeKm}km)`, { distance: distanceKm, maxRange: strikeRangeKm })
    : t("allocation.outOfRangeFormat", `超出服务半径 ${Math.abs(marginKm)}km`, { margin: Math.abs(marginKm) });

  return {
    point,
    hasPosition: true,
    distanceKm,
    strikeRangeKm,
    inRange,
    marginKm,
    statusLabel
  };
}

/**
 * 多目标对多作业单元协同分配策略解算 (Multi-Target to Multi-Unit Allocation)
 * @param {Array} targets 选中的目标列表
 * @param {Array} points 在席作业单元列表
 * @returns {Object} 包含按目标分配建议、工位负载汇总与协同说明的方案对象
 */
export function computeMultiAllocationPlan(targets, firepowerPoints) {
  if (!targets || targets.length === 0 || !firepowerPoints || firepowerPoints.length === 0) {
    return {
      hasPlan: false,
      targetPlans: [],
      batteryLoads: {},
      overallRationale: t("allocation.noAvailableUnits", "无待分配目标或无可用作业单元")
    };
  }

  const validPoints = firepowerPoints.filter(p => typeof p.latitude === "number" && typeof p.longitude === "number");
  const batteryLoads = new Map();
  const batteryCapacities = new Map();
  for (const p of firepowerPoints) {
    batteryLoads.set(p.id, 0);
    batteryCapacities.set(p.id, Math.max(1, Number(p.maxTargets) || 1));
  }

  // 1. 为每个目标解算所有可用工位的距离与包络
  const targetCandidateList = targets.map(target => {
    const candidates = validPoints.map(point => {
      const evalData = evaluateTargetToPoint(target, point);
      return {
        point,
        ...evalData
      };
    });

    // 候选工位排序规则：优先在覆盖范围内；在覆盖范围内按距离升序；超出服务半径按超距升序
    candidates.sort((a, b) => {
      if (a.inRange !== b.inRange) {
        return a.inRange ? -1 : 1;
      }
      return a.distanceKm - b.distanceKm;
    });

    return {
      target,
      candidates
    };
  });

  // 2. 考虑工位最大目标容量上限 (maxTargets) 与多工位协同均衡，进行目标到工位的匹配指派
  const targetPlans = [];

  for (const item of targetCandidateList) {
    const target = item.target;
    const candidates = item.candidates;

    if (candidates.length === 0) {
      targetPlans.push({
        target,
        recommendedPointId: firepowerPoints[0]?.id || "FIRE-01",
        recommendedPoint: firepowerPoints[0] || null,
        recommendedDistanceKm: 0,
        recommendedInRange: false,
        recommendedReason: t("allocation.noCoords", "未配置有效工位坐标"),
        evaluations: []
      });
      continue;
    }

    // 候选工位智能遴选：
    // 优先层：覆盖范围内 且 承揽数未达上限 (batteryLoads < maxTargets) 的工位
    const inRangeUnderCap = candidates.filter(
      c => c.inRange && (batteryLoads.get(c.point.id) || 0) < (batteryCapacities.get(c.point.id) || 1)
    );

    let chosen;
    if (inRangeUnderCap.length > 0) {
      chosen = inRangeUnderCap[0];
      // 如果有多处未满载候选工位，且最优候选负载已接近其上限，次优候选在覆盖范围内且距离相近(<=35%)但剩余容量更充裕，则分流促成并发调度
      if (inRangeUnderCap.length > 1) {
        const bestLoad = batteryLoads.get(chosen.point.id) || 0;
        const bestCap = batteryCapacities.get(chosen.point.id) || 1;
        const alt = inRangeUnderCap.find(c => {
          const cLoad = batteryLoads.get(c.point.id) || 0;
          const cCap = batteryCapacities.get(c.point.id) || 1;
          return (cCap - cLoad) > (bestCap - bestLoad) && c.distanceKm <= chosen.distanceKm * 1.35;
        });
        if (alt) chosen = alt;
      }
    } else {
      // 若覆盖范围内工位均已满载：优先选覆盖范围内的工位中超额最少且相对最近的；若全超服务半径则选最近工位
      const inRangeOverCap = candidates.filter(c => c.inRange);
      if (inRangeOverCap.length > 0) {
        inRangeOverCap.sort((a, b) => {
          const loadA = batteryLoads.get(a.point.id) || 0;
          const capA = batteryCapacities.get(a.point.id) || 1;
          const loadB = batteryLoads.get(b.point.id) || 0;
          const capB = batteryCapacities.get(b.point.id) || 1;
          const diffA = loadA - capA;
          const diffB = loadB - capB;
          if (diffA !== diffB) return diffA - diffB;
          return a.distanceKm - b.distanceKm;
        });
        chosen = inRangeOverCap[0];
      } else {
        chosen = candidates[0];
      }
    }

    // 更新工位负载计数
    const chosenId = chosen.point.id;
    const currentLoadBefore = batteryLoads.get(chosenId) || 0;
    const capacity = batteryCapacities.get(chosenId) || 1;
    batteryLoads.set(chosenId, currentLoadBefore + 1);
    const isOverCapacity = (currentLoadBefore + 1) > capacity;

    // 生成协同理由
    let reason = "";
    if (chosen.inRange) {
      if (isOverCapacity) {
        reason = t("allocation.reasonOverCapacity", `在覆盖范围内（距目标 ${chosen.distanceKm} km ≤ ${chosen.strikeRangeKm} km），但该工位已达单次最大目标容量上限（已分配 ${currentLoadBefore + 1}/${capacity} 个），建议核审分流或分批处置。`, {
          distance: chosen.distanceKm,
          range: chosen.strikeRangeKm,
          current: currentLoadBefore + 1,
          capacity
        });
      } else {
        reason = t("allocation.reasonOptimal", `在覆盖范围内（距目标 ${chosen.distanceKm} km ≤ ${chosen.strikeRangeKm} km），通道容量充裕（已分配 ${currentLoadBefore + 1}/${capacity} 个），响应耗时短、精度高。`, {
          distance: chosen.distanceKm,
          range: chosen.strikeRangeKm,
          current: currentLoadBefore + 1,
          capacity
        });
      }
    } else {
      reason = t("allocation.reasonClosestOutOfRange", `超出所有可用工位服务半径，该工位相对最近（超距 ${Math.abs(chosen.marginKm)} km）。`, {
        margin: Math.abs(chosen.marginKm)
      });
    }

    // 生成供该目标专用的下拉选项列表
    const evaluations = candidates.map(c => {
      const isRec = c.point.id === chosenId;
      const recTag = isRec ? " ★ 系统建议" : "";
      const cCap = batteryCapacities.get(c.point.id) || 1;
      return {
        pointId: c.point.id,
        pointName: c.point.name || c.point.id,
        distanceKm: c.distanceKm,
        inRange: c.inRange,
        marginKm: c.marginKm,
        maxTargets: cCap,
        isRecommended: isRec,
        optionText: `${c.point.name} · ${c.statusLabel} [容量上限: ${cCap}]${recTag}`
      };
    });

    targetPlans.push({
      target,
      recommendedPointId: chosenId,
      recommendedPoint: chosen.point,
      recommendedDistanceKm: chosen.distanceKm,
      recommendedInRange: chosen.inRange,
      isOverCapacity,
      maxTargets: capacity,
      recommendedReason: reason,
      evaluations
    });
  }

  // 3. 生成整体协同态势说明
  const loadSummaryParts = [];
  let inRangeCount = 0;
  for (const plan of targetPlans) {
    if (plan.recommendedInRange) inRangeCount++;
  }
  let hasAnyOverload = false;
  for (const [id, count] of batteryLoads) {
    const pt = firepowerPoints.find(p => p.id === id);
    const cap = batteryCapacities.get(id) || 1;
    if (pt && count > 0) {
      const isOver = count > cap;
      if (isOver) hasAnyOverload = true;
      loadSummaryParts.push(`【${pt.name}】${count}/${cap} 个${isOver ? '⚠️' : ''}`);
    }
  }

  const baseRatioText = inRangeCount === targets.length
    ? t("allocation.recommendAllInRange", `推荐就近分流（${targets.length} 目标均在服务半径内）`, { count: targets.length })
    : t("allocation.recommendPartialInRange", `就近分流（${inRangeCount}/${targets.length} 目标在服务半径内）`, { inRange: inRangeCount, total: targets.length });

  const overallRationale = hasAnyOverload
    ? `${baseRatioText} · 存在工位超出通道容量上限`
    : `${baseRatioText} · 各工位容量受控`;

  const loadsObj = {};
  for (const [id, count] of batteryLoads) {
    loadsObj[id] = count;
  }
  const capsObj = {};
  for (const [id, cap] of batteryCapacities) {
    capsObj[id] = cap;
  }

  return {
    hasPlan: true,
    targetPlans,
    batteryLoads: loadsObj,
    batteryCapacities: capsObj,
    overallRationale,
    loadSummary: loadSummaryParts.join(" ")
  };
}
