#include "query/plan/distributed.hpp"

#include <memory>

// TODO: Remove these includes for hacked cloning of logical operators via boost
// serialization when proper cloning is added.
#include <sstream>
#include "boost/archive/binary_iarchive.hpp"
#include "boost/archive/binary_oarchive.hpp"

#include "query/plan/operator.hpp"
#include "utils/exceptions.hpp"

namespace query::plan {

namespace {

std::pair<std::unique_ptr<LogicalOperator>, AstTreeStorage> Clone(
    const LogicalOperator &original_plan) {
  // TODO: Add a proper Clone method to LogicalOperator
  std::stringstream stream;
  {
    boost::archive::binary_oarchive out_archive(stream);
    out_archive << &original_plan;
  }
  boost::archive::binary_iarchive in_archive(stream);
  LogicalOperator *plan_copy = nullptr;
  in_archive >> plan_copy;
  return {std::unique_ptr<LogicalOperator>(plan_copy),
          std::move(in_archive.template get_helper<AstTreeStorage>(
              AstTreeStorage::kHelperId))};
}

class DistributedPlanner : public HierarchicalLogicalOperatorVisitor {
 public:
  DistributedPlanner(DistributedPlan &distributed_plan)
      : distributed_plan_(distributed_plan) {}

  using HierarchicalLogicalOperatorVisitor::PostVisit;
  using HierarchicalLogicalOperatorVisitor::PreVisit;

  // Returns true if the plan should be run on master and workers. Note, that
  // false is returned if the plan is already split.
  bool ShouldSplit() {
    // At the moment, the plan should be run on workers only if we encountered a
    // ScanAll.
    return !distributed_plan_.worker_plan && has_scan_all_;
  }

  // ScanAll are all done on each machine locally.
  bool PreVisit(ScanAll &scan) override {
    prev_ops_.push_back(&scan);
    return true;
  }
  bool PostVisit(ScanAll &) override {
    prev_ops_.pop_back();
    RaiseIfCartesian();
    RaiseIfHasWorkerPlan();
    has_scan_all_ = true;
    return true;
  }

  bool PreVisit(ScanAllByLabel &scan) override {
    prev_ops_.push_back(&scan);
    return true;
  }
  bool PostVisit(ScanAllByLabel &) override {
    prev_ops_.pop_back();
    RaiseIfCartesian();
    RaiseIfHasWorkerPlan();
    has_scan_all_ = true;
    return true;
  }
  bool PreVisit(ScanAllByLabelPropertyRange &scan) override {
    prev_ops_.push_back(&scan);
    return true;
  }
  bool PostVisit(ScanAllByLabelPropertyRange &) override {
    prev_ops_.pop_back();
    RaiseIfCartesian();
    RaiseIfHasWorkerPlan();
    has_scan_all_ = true;
    return true;
  }
  bool PreVisit(ScanAllByLabelPropertyValue &scan) override {
    prev_ops_.push_back(&scan);
    return true;
  }
  bool PostVisit(ScanAllByLabelPropertyValue &) override {
    prev_ops_.pop_back();
    RaiseIfCartesian();
    RaiseIfHasWorkerPlan();
    has_scan_all_ = true;
    return true;
  }

  // Expand is done locally on each machine with RPC calls for worker-boundary
  // crossing edges.
  bool PreVisit(Expand &exp) override {
    prev_ops_.push_back(&exp);
    return true;
  }
  // TODO: ExpandVariable

  // The following operators filter the frame or put something on it. They
  // should be worker local.
  bool PreVisit(ConstructNamedPath &op) override {
    prev_ops_.push_back(&op);
    return true;
  }
  bool PreVisit(Filter &op) override {
    prev_ops_.push_back(&op);
    return true;
  }
  bool PreVisit(ExpandUniquenessFilter<VertexAccessor> &op) override {
    prev_ops_.push_back(&op);
    return true;
  }
  bool PreVisit(ExpandUniquenessFilter<EdgeAccessor> &op) override {
    prev_ops_.push_back(&op);
    return true;
  }
  bool PreVisit(Optional &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

  // Skip needs to skip only the first N results from *all* of the results.
  // Therefore, the earliest (deepest in the plan tree) encountered Skip will
  // break the plan in 2 parts.
  //  1) Master plan with Skip and everything above it.
  //  2) Worker plan with operators below Skip, but without Skip itself.
  bool PreVisit(Skip &skip) override {
    prev_ops_.push_back(&skip);
    return true;
  }
  bool PostVisit(Skip &skip) override {
    prev_ops_.pop_back();
    if (ShouldSplit()) {
      auto input = skip.input();
      distributed_plan_.worker_plan = input;
      skip.set_input(std::make_shared<PullRemote>(
          input, distributed_plan_.plan_id,
          input->OutputSymbols(distributed_plan_.symbol_table)));
    }
    return true;
  }

  // Limit, like Skip, needs to see *all* of the results, so we split the plan.
  // Unlike Skip, we can also do the operator locally on each machine. This may
  // improve the execution speed of workers. So, the 2 parts of the plan are:
  //  1) Master plan with Limit and everything above.
  //  2) Worker plan with operators below Limit, but including Limit itself.
  bool PreVisit(Limit &limit) override {
    prev_ops_.push_back(&limit);
    return true;
  }
  bool PostVisit(Limit &limit) override {
    prev_ops_.pop_back();
    if (ShouldSplit()) {
      // Shallow copy Limit
      distributed_plan_.worker_plan = std::make_shared<Limit>(limit);
      auto input = limit.input();
      limit.set_input(std::make_shared<PullRemote>(
          input, distributed_plan_.plan_id,
          input->OutputSymbols(distributed_plan_.symbol_table)));
    }
    return true;
  }

  // OrderBy is an associative operator, this means we can do ordering
  // on workers and then merge the results on master. This requires a more
  // involved solution, so for now treat OrderBy just like Split.
  bool PreVisit(OrderBy &order_by) override {
    prev_ops_.push_back(&order_by);
    return true;
  }
  bool PostVisit(OrderBy &order_by) override {
    prev_ops_.pop_back();
    // TODO: Associative combination of OrderBy
    if (ShouldSplit()) {
      auto input = order_by.input();
      distributed_plan_.worker_plan = input;
      order_by.set_input(std::make_shared<PullRemote>(
          input, distributed_plan_.plan_id,
          input->OutputSymbols(distributed_plan_.symbol_table)));
    }
    return true;
  }

  // Treat Distinct just like Limit.
  bool PreVisit(Distinct &distinct) override {
    prev_ops_.push_back(&distinct);
    return true;
  }
  bool PostVisit(Distinct &distinct) override {
    prev_ops_.pop_back();
    if (ShouldSplit()) {
      // Shallow copy Distinct
      distributed_plan_.worker_plan = std::make_shared<Distinct>(distinct);
      auto input = distinct.input();
      distinct.set_input(std::make_shared<PullRemote>(
          input, distributed_plan_.plan_id,
          input->OutputSymbols(distributed_plan_.symbol_table)));
    }
    return true;
  }

  // TODO: Union

  // For purposes of distribution, aggregation comes in 2 flavors:
  //  * associative and
  //  * non-associative.
  //
  // Associative aggregation can be done locally on workers, and then the
  // results merged on master. Similarly to how OrderBy can be distributed. For
  // this type of aggregation, master will need to have an aggregation merging
  // operator. This need not be a new LogicalOperator, it can be a new
  // Aggregation with different Expressions.
  //
  // Non-associative aggregation needs to see all of the results and is
  // completely done on master.
  bool PreVisit(Aggregate &aggr_op) override {
    prev_ops_.push_back(&aggr_op);
    return true;
  }
  bool PostVisit(Aggregate &aggr_op) override {
    prev_ops_.pop_back();
    if (!ShouldSplit()) {
      // We have already split the plan, so the aggregation we are visiting is
      // on master.
      return true;
    }
    auto is_associative = [&aggr_op]() {
      for (const auto &aggr : aggr_op.aggregations()) {
        switch (aggr.op) {
          case Aggregation::Op::COUNT:
          case Aggregation::Op::MIN:
          case Aggregation::Op::MAX:
          case Aggregation::Op::SUM:
          case Aggregation::Op::AVG:
            break;
          default:
            return false;
        }
      }
      return true;
    };
    if (!is_associative()) {
      auto input = aggr_op.input();
      distributed_plan_.worker_plan = input;
      aggr_op.set_input(std::make_shared<PullRemote>(
          input, distributed_plan_.plan_id,
          input->OutputSymbols(distributed_plan_.symbol_table)));
      return true;
    }
    auto make_ident = [this](const auto &symbol) {
      auto *ident =
          distributed_plan_.ast_storage.Create<Identifier>(symbol.name());
      distributed_plan_.symbol_table[*ident] = symbol;
      return ident;
    };
    auto make_named_expr = [&](const auto &in_sym, const auto &out_sym) {
      auto *nexpr = distributed_plan_.ast_storage.Create<NamedExpression>(
          out_sym.name(), make_ident(in_sym));
      distributed_plan_.symbol_table[*nexpr] = out_sym;
      return nexpr;
    };
    auto make_merge_aggregation = [&](auto op, const auto &worker_sym) {
      auto *worker_ident = make_ident(worker_sym);
      auto merge_name = Aggregation::OpToString(op) +
                        std::to_string(worker_ident->uid()) + "<-" +
                        worker_sym.name();
      auto merge_sym = distributed_plan_.symbol_table.CreateSymbol(
          merge_name, false, Symbol::Type::Number);
      return Aggregate::Element{worker_ident, nullptr, op, merge_sym};
    };
    // Aggregate uses associative operation(s), so split the work across master
    // and workers.
    std::vector<Aggregate::Element> master_aggrs;
    master_aggrs.reserve(aggr_op.aggregations().size());
    std::vector<Aggregate::Element> worker_aggrs;
    worker_aggrs.reserve(aggr_op.aggregations().size());
    // We will need to create a Produce operator which moves the final results
    // from new (merge) symbols into old aggregation symbols, because
    // expressions following the aggregation expect the result in old symbols.
    std::vector<NamedExpression *> produce_exprs;
    produce_exprs.reserve(aggr_op.aggregations().size());
    for (const auto &aggr : aggr_op.aggregations()) {
      switch (aggr.op) {
        // Count, like sum, only needs to sum all of the results on master.
        case Aggregation::Op::COUNT:
        case Aggregation::Op::SUM: {
          worker_aggrs.emplace_back(aggr);
          auto merge_aggr =
              make_merge_aggregation(Aggregation::Op::SUM, aggr.output_sym);
          master_aggrs.emplace_back(merge_aggr);
          produce_exprs.emplace_back(
              make_named_expr(merge_aggr.output_sym, aggr.output_sym));
          break;
        }
        case Aggregation::Op::MIN:
        case Aggregation::Op::MAX: {
          worker_aggrs.emplace_back(aggr);
          auto merge_aggr = make_merge_aggregation(aggr.op, aggr.output_sym);
          master_aggrs.emplace_back(merge_aggr);
          produce_exprs.emplace_back(
              make_named_expr(merge_aggr.output_sym, aggr.output_sym));
          break;
        }
        // AVG is split into:
        //  * workers: SUM(xpr), COUNT(expr)
        //  * master: SUM(worker_sum) / toFloat(SUM(worker_count)) AS avg
        case Aggregation::Op::AVG: {
          auto worker_sum_sym = distributed_plan_.symbol_table.CreateSymbol(
              aggr.output_sym.name() + "_SUM", false, Symbol::Type::Number);
          Aggregate::Element worker_sum{aggr.value, aggr.key,
                                        Aggregation::Op::SUM, worker_sum_sym};
          worker_aggrs.emplace_back(worker_sum);
          auto worker_count_sym = distributed_plan_.symbol_table.CreateSymbol(
              aggr.output_sym.name() + "_COUNT", false, Symbol::Type::Number);
          Aggregate::Element worker_count{
              aggr.value, aggr.key, Aggregation::Op::COUNT, worker_count_sym};
          worker_aggrs.emplace_back(worker_count);
          auto master_sum =
              make_merge_aggregation(Aggregation::Op::SUM, worker_sum_sym);
          master_aggrs.emplace_back(master_sum);
          auto master_count =
              make_merge_aggregation(Aggregation::Op::SUM, worker_count_sym);
          master_aggrs.emplace_back(master_count);
          auto *master_sum_ident = make_ident(master_sum.output_sym);
          auto *master_count_ident = make_ident(master_count.output_sym);
          auto *to_float = distributed_plan_.ast_storage.Create<Function>(
              "TOFLOAT", std::vector<Expression *>{master_count_ident});
          auto *div_expr =
              distributed_plan_.ast_storage.Create<DivisionOperator>(
                  master_sum_ident, to_float);
          auto *as_avg = distributed_plan_.ast_storage.Create<NamedExpression>(
              aggr.output_sym.name(), div_expr);
          distributed_plan_.symbol_table[*as_avg] = aggr.output_sym;
          produce_exprs.emplace_back(as_avg);
          break;
        }
        default:
          throw utils::NotYetImplemented("distributed planning");
      }
    }
    // Rewiring is done in PostVisit(Produce), so just store our results.
    worker_aggr_ = std::make_shared<Aggregate>(
        aggr_op.input(), worker_aggrs, aggr_op.group_by(), aggr_op.remember());
    std::vector<Symbol> pull_symbols;
    pull_symbols.reserve(worker_aggrs.size() + aggr_op.remember().size());
    for (const auto &aggr : worker_aggrs)
      pull_symbols.push_back(aggr.output_sym);
    for (const auto &sym : aggr_op.remember()) pull_symbols.push_back(sym);
    auto pull_op = std::make_shared<PullRemote>(
        worker_aggr_, distributed_plan_.plan_id, pull_symbols);
    auto master_aggr_op = std::make_shared<Aggregate>(
        pull_op, master_aggrs, aggr_op.group_by(), aggr_op.remember());
    // Make our master Aggregate into Produce + Aggregate
    master_aggr_ = std::make_unique<Produce>(master_aggr_op, produce_exprs);
    return true;
  }

  bool PreVisit(Produce &produce) override {
    prev_ops_.push_back(&produce);
    return true;
  }
  bool PostVisit(Produce &produce) override {
    prev_ops_.pop_back();
    if (!master_aggr_) return true;
    // We have to rewire master/worker aggregation.
    DCHECK(worker_aggr_);
    DCHECK(ShouldSplit());
    DCHECK(std::dynamic_pointer_cast<Aggregate>(produce.input()));
    distributed_plan_.worker_plan = std::move(worker_aggr_);
    produce.set_input(std::move(master_aggr_));
    return true;
  }

  bool PreVisit(Unwind &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

  bool Visit(Once &) override { return true; }

  bool Visit(CreateIndex &) override { return true; }

  // Accumulate is used only if the query performs any writes. In such a case,
  // we need to synchronize the work done on master and all workers.
  // Synchronization will force applying changes to distributed storage, and
  // then we can continue with the rest of the plan. Currently, the remainder of
  // the plan is executed on master. In the future, when we support Cartesian
  // products after the WITH clause, we will need to split the plan in more
  // subparts to be executed on workers.
  bool PreVisit(Accumulate &acc) override {
    prev_ops_.push_back(&acc);
    return true;
  }
  bool PostVisit(Accumulate &acc) override {
    prev_ops_.pop_back();
    if (!ShouldSplit()) return true;
    if (acc.advance_command())
      throw utils::NotYetImplemented("WITH clause distributed planning");
    // Accumulate on workers, but set advance_command to false, because the
    // Synchronize operator should do that in distributed execution.
    distributed_plan_.worker_plan =
        std::make_shared<Accumulate>(acc.input(), acc.symbols(), false);
    // Create a synchronization point. Use pull remote to fetch accumulated
    // symbols from workers. Local input operations are the same as on workers.
    auto pull_remote = std::make_shared<PullRemote>(
        nullptr, distributed_plan_.plan_id, acc.symbols());
    auto sync = std::make_shared<Synchronize>(
        distributed_plan_.worker_plan, pull_remote, acc.advance_command());
    auto *prev_op = prev_ops_.back();
    // Wire the previous operator (on master) into our synchronization operator.
    // TODO: Find a better way to replace the previous operation's input than
    // using dynamic casting.
    if (auto *produce = dynamic_cast<Produce *>(prev_op)) {
      produce->set_input(sync);
    } else if (auto *aggr_op = dynamic_cast<Aggregate *>(prev_op)) {
      aggr_op->set_input(sync);
    } else {
      throw utils::NotYetImplemented("WITH clause distributed planning");
    }
    return true;
  }

  bool PreVisit(CreateNode &op) override {
    // TODO: Creation needs to be modified if running on master, so as to
    // distribute node creation to workers.
    prev_ops_.push_back(&op);
    return true;
  }

  bool PreVisit(CreateExpand &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

  bool PreVisit(Delete &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

  bool PreVisit(SetProperty &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

  bool PreVisit(SetProperties &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

  bool PreVisit(SetLabels &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

  bool PreVisit(RemoveProperty &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

  bool PreVisit(RemoveLabels &op) override {
    prev_ops_.push_back(&op);
    return true;
  }

 protected:
  bool DefaultPreVisit() override {
    throw utils::NotYetImplemented("distributed planning");
  }

  bool DefaultPostVisit() override {
    prev_ops_.pop_back();
    return true;
  }

 private:
  DistributedPlan &distributed_plan_;
  // Used for rewiring the master/worker aggregation in PostVisit(Produce)
  std::shared_ptr<LogicalOperator> worker_aggr_;
  std::unique_ptr<LogicalOperator> master_aggr_;
  std::vector<LogicalOperator *> prev_ops_;
  bool has_scan_all_ = false;

  void RaiseIfCartesian() {
    if (has_scan_all_)
      throw utils::NotYetImplemented("Cartesian product distributed planning");
  }

  void RaiseIfHasWorkerPlan() {
    if (distributed_plan_.worker_plan)
      throw utils::NotYetImplemented("distributed planning");
  }
};

}  // namespace

DistributedPlan MakeDistributedPlan(const LogicalOperator &original_plan,
                                    const SymbolTable &symbol_table,
                                    std::atomic<int64_t> &next_plan_id) {
  DistributedPlan distributed_plan;
  // If we will generate multiple worker plans, we will need to increment the
  // next_plan_id for each one.
  distributed_plan.plan_id = next_plan_id++;
  distributed_plan.symbol_table = symbol_table;
  std::tie(distributed_plan.master_plan, distributed_plan.ast_storage) =
      Clone(original_plan);
  DistributedPlanner planner(distributed_plan);
  distributed_plan.master_plan->Accept(planner);
  if (planner.ShouldSplit()) {
    // We haven't split the plan, this means that it should be the same on
    // master and worker. We only need to prepend PullRemote to master plan.
    distributed_plan.worker_plan = std::move(distributed_plan.master_plan);
    distributed_plan.master_plan = std::make_unique<PullRemote>(
        distributed_plan.worker_plan, distributed_plan.plan_id,
        distributed_plan.worker_plan->OutputSymbols(
            distributed_plan.symbol_table));
  }
  return distributed_plan;
}

}  // namespace query::plan