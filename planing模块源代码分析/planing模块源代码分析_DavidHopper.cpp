 Reference:
https://blog.csdn.net/davidhopper/article/details/89360385
https://blog.csdn.net/davidhopper/article/details/79176505


1.
	规划（Planning）模块位于命名空间：apollo::planning，其作用在于构建无人车从起点到终的局部行驶路径，
	具体而言，就是给定导航地图、导航路径、当前定位点、车辆状态（包括：位置、速度、加速度、底盘）、 周边目标的感知及预测信息（如交通标志和障碍物等），规划模块计算出可供控制模块（Controller）执行的一条安全且舒适的行驶路径。
	注意，规划模块输出的路径是局部路径而非全局路径。
	举个简单示例加以说明，假如无人车需从长沙智能驾驶研究院行驶至长沙高铁南站，首先需借助Routing（路由寻径）模块输出全局导航路径，接下来才是规划模块基于全局导航路径进行一小段、一小段具体行驶路径的规划



2. // 模块主入口
	Planning功能模块的启动命令为：
	/apollo/bazel - bin/cyber/mainboard - p compute_sched - d  /apollo/modules/planning/dag/planning.dag

	- p compute_sched表明使用配置文件 /apollo/cyber/conf/compute_sched.conf 进行任务调度，该参数可忽略
	- d /apollo/modules/planning/dag/planning.dag 表明动态加载的是Planning模块
	Planning模块的主入口为： /apollo/cyber/mainboard/mainboard.cc


3. // /apollo/cyber/mainboard/mainboard.cc

	int main(int argc, char** argv) {
		google::SetUsageMessage("we use this program to load dag and run user apps.");
		// parse the argument
		ModuleArgument module_args;
		module_args.ParseArgument(argc, argv);   //main函数十分简单，首先是解析参数												
		apollo::cyber::Init(argv[0]);			 // start module // 初始化cyber环境  // initialize cyber  
		ModuleController controller(module_args);  //接下来创建一个ModuleController类对象controller
		if (!controller.Init()) {  				//调用controller.Init()启动相关功能模块,ModuleController::Init()函数十分简单，内部调用了ModuleController::LoadAll()函数
			controller.Clear();
			AERROR << "module start error.";
			return -1;
		}
		apollo::cyber::WaitForShutdown();		// 进入Cyber RT的消息循环，直到等待cyber::WaitForShutdown()返回
		controller.Clear();
		AINFO << "exit mainboard.";
		return 0;
	}



4. // cyber/mainboard/module_controller.cc
   // LoadAll
	bool ModuleController::LoadAll() {
		const std::string work_root = common::WorkRoot();
		const std::string current_path = common::GetCurrentPath();
		const std::string dag_root_path = common::GetAbsolutePath(work_root, "dag");
		for (auto& dag_conf : args_.GetDAGConfList()) {
			std::string module_path = "";
			if (dag_conf == common::GetFileName(dag_conf)) {
				// case dag conf argument var is a filename
				module_path = common::GetAbsolutePath(dag_root_path, dag_conf);
			}
			else if (dag_conf[0] == '/') {
				// case dag conf argument var is an absolute path
				module_path = dag_conf;
			}
			else {
				// case dag conf argument var is a relative path
				module_path = common::GetAbsolutePath(current_path, dag_conf);
				if (!common::PathExists(module_path)) {
					module_path = common::GetAbsolutePath(work_root, dag_conf);
				}
			}
			AINFO << "Start initialize dag: " << module_path;
			if (!LoadModule(module_path)) {
				AERROR << "Failed to load module: " << module_path;
				return false;
			}
		}
		return true;
	}

	上述函数处理一个dag_conf配置文件循环，读取配置文件中的所有dag_conf，并逐一调用bool ModuleController::LoadModule(const std::string& path)函数加载功能模块。


// apollo::planning::PlanningComponent类对象的创建过程


5.Cyber RT使用工厂设计模式创建apollo::planning::PlanningComponent类对象


6. apollo::planning::PlanningComponent类的注册过程

使用工厂模式动态创建apollo::planning::PlanningComponent类对象，应先生成一个与之对应的工厂类apollo::cyber::class_loader::utility::ClassFactory<apollo::planning::PlanningComponent, apollo::cyber::ComponentBase >，
并将其加入到工厂集合类std::map<std::string, utility::AbstractClassFactoryBase*>中，这个就是 apollo::planning::PlanningComponent类的注册过程，下面具体阐述之，主要流程如下所示：







7. apollo::planning::PlanningComponent类对象的动态创建过程











8.
	//具体规划算法分析

	Apollo 3.5将规划分为两种模式： 
		OnLanePlanning（车道规划，可用于城区及高速公路各种复杂道路）
		NaviPlanning（导航规划，主要用于高速公路）

	根据Apollo团队的最新开发思路，今后只会保留一个规划算法：PublicRoadPlanner
	包含四种具体规划算法：
		1. PublicRoadPlanner（即以前的EMPlanner，是Apollo 3.5的主用规划算法）
		2. LatticePlanner（Apollo 3.5的第二重要规划算法，成熟度不足，里面的一些优秀算法思想将被融合到PublicRoadPlanner中，今后该算法将不再维护）
		3. NaviPlanner（百度美研与长沙智能驾驶研究院合作开发，主要用于高速公路场景）
		4. RTKPlanner（循迹算法，一般不用。如需循迹，可使用Python脚本程序 modules/tools/record_play/rtk_player.py）






9. // PublicRoadPlanner

PublicRoadPlanner算法从Routing模块输出的高精地图Lane序列获得全局导航路径，基于场景(Scenario)的理念进行局部行驶轨迹规划。每个场景又包含若干个阶段(Stage)，在每个阶段均使用若干个任务(Task)生成局部行驶轨迹.
基于场景(Scenario)、阶段(Stage)和任务(Task)的理念进行规划，优点是能合理有效地应对每种场景，易于扩充，并且基于配置文件动态增减场景、阶段及使用的任务，灵活性强；缺点是可能会遗漏一些特殊场景，但可通过不断扩充新的场景加以解决

// 场景(Scenario)
具体而言，将公共道路行驶划分为:
	1.BareIntersectionUnprotectedScenario(裸露交叉路口无保护场景，即没有红绿灯及交通标志的交叉路口场景，感谢Apollo美研团队Yifei Jiang老师的答疑)
	2.LaneFollowScenario(跟车场景)
	3.NarrowStreetUTurnScenario(狭窄街道调头场景，暂未实现)
	4.SidePassScenario(侧向通行场景，即前方有停止车辆，借道绕行后再回原车道)
	5.StopSignUnprotectedScenario(停止标志无保护场景)
	6.TrafficLightProtectedScenario(红绿灯保护场景)
	7.TrafficLightUnprotectedLeftTurnScenario(红绿灯无保护左转弯场景)
	8.TrafficLightUnprotectedRightTurnScenario(红绿灯无保护右转弯场景)
	9.PullOverScenario（靠边停车场景）
	10.ValetParkingScenario(泊车场景)等多个场景

// 阶段(Stage)
每个场景又包含若干个阶段(Stage)，在每个阶段均使用若干个任务(Task)生成局部行驶轨迹:

	BareIntersectionUnprotectedScenario场景包含: BareIntersectionUnprotectedStageApproach、BareIntersectionUnprotectedStageIntersectionCruise两个阶段；
	LaneFollowScenario场景包含: LaneFollowStage一个阶段；
	SidePassScenario场景包含: StageApproachObstacle、StageDetectSafety、StageGeneratePath、StageStopOnWaitPoint、StagePassObstacle、StageBackup六个阶段；
	StopSignUnprotectedScenario场景包含: StopSignUnprotectedStagePreStop、StopSignUnprotectedStageStop、StopSignUnprotectedStageCreep、StopSignUnprotectedStageIntersectionCruise四个阶段；
	TrafficLightProtectedScenario场景包含: TrafficLightProtectedStageApproach、TrafficLightProtectedStageIntersectionCruise两个阶段；
	TrafficLightUnprotectedLeftTurnScenario场景包含: TrafficLightUnprotectedLeftTurnStageStop、TrafficLightUnprotectedLeftTurnStageCreep、TrafficLightUnprotectedLeftTurnStageIntersectionCruise三个阶段；
	TrafficLightUnprotectedRightTurnScenario场景包含: TrafficLightUnprotectedRightTurnStageStop、TrafficLightUnprotectedRightTurnStageCreep、TrafficLightUnprotectedRightTurnStageIntersectionCruise三个阶段；
	PullOverScenario场景包含: PullOverStageApproach一个阶段（尚未开发完毕）；
	ValetParkingScenario场景包含: StageApproachingParkingSpot、StageParking两个阶段。


// 任务(Task)
任务分为决策（Decider）与优化（Optimizer ）两类:
	其中决策类任务包含PathLaneBorrowDecider, SpeedLimitDecider等（所有决策类任务均包含于modules / planning / tasks / deciders目录）
	优化类任务包含DpPolyPathOptimizer、DpStSpeedOptimizer等（所有优化类任务均包含于modules / planning / tasks / optimizers目录）

任意一个场景中的任意一个阶段均会利用上述两类任务的若干种:
	例如，BareIntersectionUnprotectedScenario场景中的BareIntersectionUnprotectedStageApproach阶段使用了PathDecider、SpeedBoundsDecider、DpStSpeedOptimizer、
	SpeedDecider、SpeedBoundsDecider等决策任务及DpPolyPathOptimizer、DpPolyPathOptimizer等优化任务（见配置文件modules / planning / conf / scenario / bare_intersection_unprotected_config.pb.txt）





10.  // GDB

	可借助GDB调试命令对上述执行流程进行更为深入的理解，例如TrafficLightProtectedScenario场景中TrafficLightProtectedStageApproach阶段的PathLaneBorrowDecider任务的调用堆栈如下，从下往上看，对于任意一个任务的调用流程一目了然：
		
		#0  apollo::planning::PathLaneBorrowDecider::Process(this = 0x7f8c28294460, frame = 0x7f8c38029f70,
			reference_line_info = 0x7f8c3802b140) at modules / planning / tasks / deciders / path_lane_borrow_decider / path_lane_borrow_decider.cc:39
		#1  0x00007f8c0468b7c8 in apollo::planning::Decider::Execute(this = 0x7f8c28294460, frame = 0x7f8c38029f70,
			reference_line_info = 0x7f8c3802b140) at modules / planning / tasks / deciders / decider.cc:31
		#2  0x00007f8c065c4a01 in apollo::planning::scenario::Stage::ExecuteTaskOnReferenceLine(this = 0x7f8c28293eb0,
			planning_start_point = ..., frame = 0x7f8c38029f70) at modules / planning / scenarios / stage.cc:96
		#3  0x00007f8c06e721da in apollo::planning::scenario::traffic_light::TrafficLightProtectedStageApproach::Process(
			this = 0x7f8c28293eb0, planning_init_point = ..., frame = 0x7f8c38029f70) at
			modules / planning / scenarios / traffic_light / protected / stage_approach.cc:48
		#4  0x00007f8c067f1732 in apollo::planning::scenario::Scenario::Process(
			this = 0x7f8c2801bf20, planning_init_point = ..., frame = 0x7f8c38029f70)
			at modules / planning / scenarios / scenario.cc:76
		#5  0x00007f8c186e153a in apollo::planning::PublicRoadPlanner::Plan(
			this = 0x23093de0, planning_start_point = ..., frame = 0x7f8c38029f70,
			ptr_computed_trajectory = 0x7f8b9a5fbed0) at modules / planning / planner / public_road / public_road_planner.cc:51
		#6  0x00007f8c19ee5937 in apollo::planning::OnLanePlanning::Plan(
			this = 0x237f3b0, current_time_stamp = 1557133995.3679764, stitching_trajectory = std::vector of length 1,
			capacity 1 = { ... }, ptr_trajectory_pb = 0x7f8b9a5fbed0)  at modules / planning / on_lane_planning.cc:436
		#7  0x00007f8c19ee40fa in apollo::planning::OnLanePlanning::RunOnce(
			this = 0x237f3b0, local_view = ..., ptr_trajectory_pb = 0x7f8b9a5fbed0) at modules / planning / on_lane_planning.cc:304
		#8  0x00007f8c1ab0d494 in apollo::planning::PlanningComponent::Proc(
			this = 0x1d0f310, prediction_obstacles = std::shared_ptr(count 4, weak 0) 0x7f8b840164f8,
			chassis = std::shared_ptr(count 4, weak 0) 0x7f8b84018a08,
			localization_estimate = std::shared_ptr(count 4, weak 0) 0x7f8b8400d3b8) at modules / planning / planning_component.cc:134
		#9  0x00007f8c1abb46c4 in apollo::cyber::Component<apollo::prediction::PredictionObstacles,
			apollo::canbus::Chassis, apollo::localization::LocalizationEstimate, apollo::cyber::NullType>::Process(this = 0x1d0f310,
			msg0 = std::shared_ptr(count 4, weak 0) 0x7f8b840164f8, msg1 = std::shared_ptr(count 4, weak 0) 0x7f8b84018a08,
			msg2 = std::shared_ptr(count 4, weak 0) 0x7f8b8400d3b8) at . / cyber / component / component.h:291
		#10 0x00007f8c1aba2698 in apollo::cyber::Component<apollo::prediction::PredictionObstacles,
			apollo::canbus::Chassis, apollo::localization::LocalizationEstimate, apollo::cyber::NullType>::Initialize(
			apollo::cyber::proto::ComponentConfig const&)::{lambda(std::shared_ptr<apollo::prediction::PredictionObstacles> const&,
				std::shared_ptr<apollo::canbus::Chassis> const&, std::shared_ptr<apollo::localization::LocalizationEstimate> const&)#2}::operator()
				(std::shared_ptr<apollo::prediction::PredictionObstacles> const&, std::shared_ptr<apollo::canbus::Chassis> const&,
					std::shared_ptr<apollo::localization::LocalizationEstimate> const&) const (__closure = 0x2059a430,
						msg0 = std::shared_ptr(count 4, weak 0) 0x7f8b840164f8, msg1 = std::shared_ptr(count 4, weak 0) 0x7f8b84018a08,
						msg2 = std::shared_ptr(count 4, weak 0) 0x7f8b8400d3b8) at . / cyber / component / component.h:378
		#11 0x00007f8c1abb4ad2 in apollo::cyber::croutine::RoutineFactory apollo::cyber::croutine::CreateRoutineFactory
			<apollo::prediction::PredictionObstacles, apollo::canbus::Chassis, apollo::localization::LocalizationEstimate,
			apollo::cyber::Component<apollo::prediction::PredictionObstacles, apollo::canbus::Chassis,
			apollo::localization::LocalizationEstimate, apollo::cyber::NullType>::Initialize(
			apollo::cyber::proto::ComponentConfig const&)::{lambda(std::shared_ptr<apollo::prediction::PredictionObstacles> const&,
				std::shared_ptr<apollo::canbus::Chassis> const&, std::shared_ptr<apollo::localization::LocalizationEstimate> const&)#2}&>
				(apollo::cyber::Component<apollo::prediction::PredictionObstacles, apollo::canbus::Chassis,
					apollo::localization::LocalizationEstimate, apollo::cyber::NullType>::Initialize(apollo::cyber::proto::ComponentConfig const&)::
			{lambda(std::shared_ptr<apollo::prediction::PredictionObstacles> const&, std::shared_ptr<apollo::canbus::Chassis> const&,
			std::shared_ptr<apollo::localization::LocalizationEstimate> const&)#2}&,
					std::shared_ptr<apollo::cyber::data::DataVisitor<apollo::prediction::PredictionObstacles,
					apollo::canbus::Chassis, apollo::localization::LocalizationEstimate, apollo::cyber::NullType> > const&)::
			{lambda()#1}::operator()() const::{lambda()#1}::operator()() const (__closure = 0x2059a420) at . / cyber / croutine / routine_factory.h:108
		#12 0x00007f8c1ac0466a in std::_Function_handler<void(), apollo::cyber::croutine::RoutineFactory
			apollo::cyber::croutine::CreateRoutineFactory<apollo::prediction::PredictionObstacles, apollo::canbus::Chassis, apollo::localization::LocalizationEstimate,
			apollo::cyber::Component<apollo::prediction::PredictionObstacles, apollo::canbus::Chassis, apollo::localization::LocalizationEstimate,
			apollo::cyber::NullType>::Initialize(apollo::cyber::proto::ComponentConfig const&)::{lambda(std::shared_ptr<apollo::prediction::PredictionObstacles> const&,
			std::shared_ptr<apollo::canbus::Chassis> const&, std::shared_ptr<apollo::localization::LocalizationEstimate> const&)#2}&>
			(apollo::cyber::Component<apollo::prediction::PredictionObstacles, apollo::canbus::Chassis, apollo::localization::LocalizationEstimate,
				apollo::cyber::NullType>::Initialize(apollo::cyber::proto::ComponentConfig const&)::{lambda(std::shared_ptr<apollo::prediction::PredictionObstacles> const&,
					std::shared_ptr<apollo::canbus::Chassis> const&, std::shared_ptr<apollo::localization::LocalizationEstimate> const&)#2}&,
				std::shared_ptr<apollo::cyber::data::DataVisitor<apollo::prediction::PredictionObstacles, apollo::canbus::Chassis, apollo::localization::LocalizationEstimate,
				apollo::cyber::NullType> > const&)::{lambda()#1}::operator()() const::{lambda()#1}>::_M_invoke(std::_Any_data const&) (__functor = ...) at
			/ usr / include / c++ / 4.8 / functional:2071
		#13 0x00007f8c5f5b86e8 in std::function<void()>::operator()() const (this = 0x205f1160) at / usr / include / c++ / 4.8 / functional:2471
		#14 0x00007f8c57560cbc in apollo::cyber::croutine::CRoutine::Run(this = 0x205f1148) at . / cyber / croutine / croutine.h:143
		#15 0x00007f8c5755ff55 in apollo::cyber::croutine::(anonymous namespace)::CRoutineEntry(arg = 0x205f1148) at cyber / croutine / croutine.cc:43
	




11.	与PublicRoadPlanner规划算法相关的有两处: 1. PublicRoadPlanner::Init, 2.  PublicRoadPlanner::Plan

	// PublicRoadPlanner::Init:
	在PublicRoadPlanner::Init函数内，首先读取配置文件 / apollo / modules / planning / conf / planning_config.pb.txt，获取所有支持的场景supported_scenarios，然后调用scenario_manager_.Init(supported_scenarios); 
	对这些场景进行初始化，具体而言就是先调用ScenarioManager::RegisterScenarios函数将配置文件中的所有场景添加到场景管理器对象scenario::ScenarioManager scenario_manager_中，
		再调用ScenarioManager::CreateScenario函数，生成当前路况对应的场景对象std::unique_ptr<Scenario> current_scenario_。
		
	// modules / planning / planner / public_road / public_road_planner.cc

	Status PublicRoadPlanner::Init(const PlanningConfig& config) {
		config_ = config;	 //首先读取配置文件/apollo/modules/planning/conf/planning_config.pb.txt，获取所有支持的场景supported_scenarios
		std::set<ScenarioConfig::ScenarioType> supported_scenarios;
		const auto& public_road_config = config_.standard_planning_config().planner_public_road_config();
		for (int i = 0; i < public_road_config.scenario_type_size(); ++i) {
			const ScenarioConfig::ScenarioType scenario = public_road_config.scenario_type(i);
			supported_scenarios.insert(scenario);
		}
		scenario_manager_.Init(supported_scenarios); //调用scenario_manager_.Init(supported_scenarios);对这些场景进行初始化，具体而言就是先调用ScenarioManager::RegisterScenarios函数将配置文件中的所有场景添加到场景管理器对象scenario::ScenarioManager scenario_manager_中，再调用ScenarioManager::CreateScenario函数，生成当前路况对应的场景对象
		return Status::OK();
	}
	
	bool ScenarioManager::Init(
		const std::set<ScenarioConfig::ScenarioType>& supported_scenarios) {
		RegisterScenarios();  // 调用ScenarioManager::RegisterScenarios函数将配置文件中的所有场景添加到场景管理器对象scenario::ScenarioManager scenario_manager_中
		default_scenario_type_ = ScenarioConfig::LANE_FOLLOW;
		supported_scenarios_ = supported_scenarios;
		current_scenario_ = CreateScenario(default_scenario_type_); //调用ScenarioManager::CreateScenario函数，生成当前路况对应的场景对象std::unique_ptr<Scenario> current_scenario_
		return true;
	}
	
	void ScenarioManager::RegisterScenarios() {
		CHECK(Scenario::LoadConfig(FLAGS_scenario_lane_follow_config_file, &config_map_[ScenarioConfig::LANE_FOLLOW]));
		CHECK(Scenario::LoadConfig(FLAGS_scenario_side_pass_config_file, &config_map_[ScenarioConfig::SIDE_PASS]));
		CHECK(Scenario::LoadConfig(FLAGS_scenario_stop_sign_unprotected_config_file, &config_map_[ScenarioConfig::STOP_SIGN_UNPROTECTED]));
		CHECK(Scenario::LoadConfig(FLAGS_scenario_traffic_light_right_turn_unprotected_config_file, &config_map_[ScenarioConfig::TRAFFIC_LIGHT_RIGHT_TURN_UNPROTECTED]));
	}


	 // apollo / modules / planning / scenarios / scenario_manager.cc

	ScenarioManager::ScenarioDispatch使用Strategy设计模式来分派具体的场景



	void ScenarioManager::ScenarioDispatch(const common::TrajectoryPoint& ego_point, const Frame& frame) {

	}

	std::unique_ptr<Scenario> ScenarioManager::CreateScenario(
		ScenarioConfig::ScenarioType scenario_type) {
		std::unique_ptr<Scenario> ptr;
		switch (scenario_type) {
		case ScenarioConfig::LANE_FOLLOW:
			ptr.reset(new lane_follow::LaneFollowScenario(config_map_[scenario_type], &scenario_context_));
			break;
		case ScenarioConfig::SIDE_PASS:
			ptr.reset(new scenario::side_pass::SidePassScenario(config_map_[scenario_type], &scenario_context_));
			break;
		case ScenarioConfig::STOP_SIGN_UNPROTECTED:
			ptr.reset(new scenario::stop_sign::StopSignUnprotectedScenario(config_map_[scenario_type], &scenario_context_));
			break;
		case ScenarioConfig::TRAFFIC_LIGHT_RIGHT_TURN_UNPROTECTED:
			ptr.reset(new scenario::traffic_light::TrafficLightRightTurnUnprotectedScenario(config_map_[scenario_type], &scenario_context_));
			break;
		default:
			return nullptr;
		}
		if (ptr != nullptr) {
			ptr->Init();
		}
		return ptr;
	}





12. // PublicRoadPlanner::Plan
PublicRoadPlanner::Plan函数内:
首先调用函数 ScenarioManager : Update 根据实时路况更新当前场景对象std::unique_ptr<Scenario> current_scenario_，
接着调用scenario_->Process(planning_start_point, frame)语句实施具体的场景算法。
如果Scenario::Process函数的返回值是scenario::Scenario::STATUS_DONE，表明当前场景状态已完成，则再次调用函数ScenarioManager::Update更新当前场景，否则继续处理当前场景并返回。


Status PublicRoadPlanner::Plan(const TrajectoryPoint& planning_start_point, Frame* frame) {
	DCHECK_NOTNULL(frame);
	scenario_manager_.Update(planning_start_point, *frame); //调用函数ScenarioManager:Update根据实时路况更新当前场景对象std::unique_ptr<Scenario> current_scenario_
	scenario_ = scenario_manager_.mutable_scenario();
	auto result = scenario_->Process(planning_start_point, frame); // 调用scenario_->Process(planning_start_point, frame)语句实施具体的场景算法

	if (FLAGS_enable_record_debug) {
		if (frame->output_trajectory()) {
			auto scenario_debug = frame->output_trajectory()->mutable_debug()->mutable_planning_data()->mutable_scenario();
			scenario_debug->set_scenario_type(scenario_->scenario_type());
			scenario_debug->set_stage_type(scenario_->GetStage());
			scenario_debug->set_msg(scenario_->GetMsg());
		}
	}
	if (result == scenario::Scenario::STATUS_DONE) {
		// only updates scenario manager when previous scenario's status is
		// STATUS_DONE
		scenario_manager_.Update(planning_start_point, *frame);
	}
	else if (result == scenario::Scenario::STATUS_UNKNOWN) {
		return Status(common::PLANNING_ERROR, "scenario returned unknown");
	}
	return Status::OK();
}




13. // 看场景更新函数 ScenarioManager::Update 的代码

// apollo / modules / planning / scenarios / scenario_manager.cc

void ScenarioManager::Update(const common::TrajectoryPoint& ego_point, const Frame& frame) {
	CHECK(!frame.reference_line_info().empty());
	Observe(frame);
	ScenarioDispatch(ego_point, frame);
}

该函数逻辑很简单，包含两个子函数：ScenarioManager::Observe和ScenarioManager::ScenarioDispatch

ScenarioManager::Observe: 用于更新first_encountered_overlap_map_（车辆沿着参考线行驶首次遇到的道路连接的键值对，key表示道路连接类型，例如：PNC_JUNCTION（用于规划控制模块的交叉路口，是一个由多条道路停止线包围而成的多边形区域，
、SIGNAL（红绿灯） 、STOP_SIGN（停止标志）、YIELD_SIGN（让行标志），value表示对应的地图元素数据）


//  用于更新first_encountered_overlap_map_
void ScenarioManager::Observe(const Frame& frame) {
	// init first_encountered_overlap_map_
	first_encountered_overlap_map_.clear();
	const auto& reference_line_info = frame.reference_line_info().front();
	const auto& first_encountered_overlaps = reference_line_info.FirstEncounteredOverlaps();
	for (const auto& overlap : first_encountered_overlaps) {
		if (overlap.first == ReferenceLineInfo::PNC_JUNCTION ||
			overlap.first == ReferenceLineInfo::SIGNAL ||
			overlap.first == ReferenceLineInfo::STOP_SIGN ||
			overlap.first == ReferenceLineInfo::YIELD_SIGN) {
			first_encountered_overlap_map_[overlap.first] = overlap.second;
		}
	}
}



modules / planning / scenarios / scenario_manager.cc
// ScenarioManager::ScenarioDispatch使用Strategy设计模式来分派具体的场景

void ScenarioManager::ScenarioDispatch(const common::TrajectoryPoint& ego_point, const Frame& frame) {
	CHECK(!frame.reference_line_info().empty());

	////////////////////////////////////////
	// default: LANE_FOLLOW
	ScenarioConfig::ScenarioType scenario_type = default_scenario_type_;

	// check current_scenario (not switchable)
	switch (current_scenario_->scenario_type()) {
	case ScenarioConfig::LANE_FOLLOW:
	case ScenarioConfig::CHANGE_LANE:
	case ScenarioConfig::PULL_OVER:
		break;
	case ScenarioConfig::BARE_INTERSECTION_UNPROTECTED:
	case ScenarioConfig::PARK_AND_GO:
	case ScenarioConfig::STOP_SIGN_PROTECTED:
	case ScenarioConfig::STOP_SIGN_UNPROTECTED:
	case ScenarioConfig::TRAFFIC_LIGHT_PROTECTED:
	case ScenarioConfig::TRAFFIC_LIGHT_UNPROTECTED_LEFT_TURN:
	case ScenarioConfig::TRAFFIC_LIGHT_UNPROTECTED_RIGHT_TURN:
	case ScenarioConfig::VALET_PARKING:
		// must continue until finish
		if (current_scenario_->GetStatus() !=
			Scenario::ScenarioStatus::STATUS_DONE) {
			scenario_type = current_scenario_->scenario_type();
		}
		break;
	default:
		break;
	}

	////////////////////////////////////////
	// ParkAndGo / starting scenario
	if (scenario_type == default_scenario_type_) {
		if (FLAGS_enable_scenario_park_and_go) {
			scenario_type = SelectParkAndGoScenario(frame);
		}
	}

	////////////////////////////////////////
	// intersection scenarios
	if (scenario_type == default_scenario_type_) {
		hdmap::PathOverlap* traffic_sign_overlap = nullptr;
		hdmap::PathOverlap* pnc_junction_overlap = nullptr;
		ReferenceLineInfo::OverlapType overlap_type;

		const auto& reference_line_info = frame.reference_line_info().front();
		const auto& first_encountered_overlaps =
			reference_line_info.FirstEncounteredOverlaps();
		// note: first_encountered_overlaps already sorted
		for (const auto& overlap : first_encountered_overlaps) {
			if (overlap.first == ReferenceLineInfo::SIGNAL ||
				overlap.first == ReferenceLineInfo::STOP_SIGN ||
				overlap.first == ReferenceLineInfo::YIELD_SIGN) {
				overlap_type = overlap.first;
				traffic_sign_overlap = const_cast<hdmap::PathOverlap*>(&overlap.second);
				break;
			}
			else if (overlap.first == ReferenceLineInfo::PNC_JUNCTION) {
				pnc_junction_overlap = const_cast<hdmap::PathOverlap*>(&overlap.second);
			}
		}

		if (traffic_sign_overlap) {
			switch (overlap_type) {
			case ReferenceLineInfo::STOP_SIGN:
				if (FLAGS_enable_scenario_stop_sign) {
					scenario_type =
						SelectStopSignScenario(frame, *traffic_sign_overlap);
				}
				break;
			case ReferenceLineInfo::SIGNAL:
				if (FLAGS_enable_scenario_traffic_light) {
					scenario_type =
						SelectTrafficLightScenario(frame, *traffic_sign_overlap);
				}
				break;
			case ReferenceLineInfo::YIELD_SIGN:
				// TODO(all): to be added
				// scenario_type = SelectYieldSignScenario(
				//     frame, *traffic_sign_overlap);
				break;
			default:
				break;
			}
		}
		else if (pnc_junction_overlap) {
			// bare intersection
			if (FLAGS_enable_scenario_bare_intersection) {
				scenario_type =
					SelectBareIntersectionScenario(frame, *pnc_junction_overlap);
			}
		}
	}

	////////////////////////////////////////
	// CHANGE_LANE scenario
	if (scenario_type == default_scenario_type_) {
		scenario_type = SelectChangeLaneScenario(frame);
	}

	////////////////////////////////////////
	// pull-over scenario
	if (scenario_type == default_scenario_type_) {
		if (FLAGS_enable_scenario_pull_over) {
			scenario_type = SelectPullOverScenario(frame);
		}
	}

	////////////////////////////////////////
	// VALET_PARKING scenario
	if (scenario_type == default_scenario_type_) {
		scenario_type = SelectValetParkingScenario(frame);
	}

	ADEBUG << "select scenario: "
		<< ScenarioConfig::ScenarioType_Name(scenario_type);

	// update PlanningContext
	UpdatePlanningContext(frame, scenario_type);

	if (current_scenario_->scenario_type() != scenario_type) {
		current_scenario_ = CreateScenario(scenario_type);
	}
}

s


// https://blog.csdn.net/davidhopper/article/details/79176505
14.




