import { initDataManagement } from "./data-management.js";
import { eventBus } from "../core/event-bus.js";

initDataManagement();
eventBus.emit("management:open");
