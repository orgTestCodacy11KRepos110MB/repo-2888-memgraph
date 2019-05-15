#include <random>
#include <string>

#include <benchmark/benchmark.h>

#include "communication/result_stream_faker.hpp"
#include "database/graph_db.hpp"
#include "database/graph_db_accessor.hpp"
#include "query/frontend/opencypher/parser.hpp"
#include "query/frontend/semantic/required_privileges.hpp"
#include "query/frontend/semantic/symbol_generator.hpp"
#include "query/interpreter.hpp"
#include "query/plan/planner.hpp"

// The following classes are wrappers for utils::MemoryResource, so that we can
// use BENCHMARK_TEMPLATE

class MonotonicBufferResource final {
  utils::MonotonicBufferResource memory_{query::kExecutionMemoryBlockSize};

 public:
  utils::MemoryResource *get() { return &memory_; }
};

class NewDeleteResource final {
 public:
  utils::MemoryResource *get() { return utils::NewDeleteResource(); }
};

static void AddVertices(database::GraphDb *db, int vertex_count) {
  auto dba = db->Access();
  for (int i = 0; i < vertex_count; i++) dba.InsertVertex();
  dba.Commit();
}

static const char *kStartLabel = "start";

static void AddStarGraph(database::GraphDb *db, int spoke_count, int depth) {
  auto dba = db->Access();
  VertexAccessor center_vertex = dba.InsertVertex();
  center_vertex.add_label(dba.Label(kStartLabel));
  for (int i = 0; i < spoke_count; ++i) {
    VertexAccessor prev_vertex = center_vertex;
    for (int j = 0; j < depth; ++j) {
      auto dest = dba.InsertVertex();
      dba.InsertEdge(prev_vertex, dest, dba.EdgeType("Type"));
      prev_vertex = dest;
    }
  }
  dba.Commit();
}

static void AddTree(database::GraphDb *db, int vertex_count) {
  auto dba = db->Access();
  std::vector<VertexAccessor> vertices;
  vertices.reserve(vertex_count);
  auto root = dba.InsertVertex();
  root.add_label(dba.Label(kStartLabel));
  vertices.push_back(root);
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
  std::mt19937_64 rg(42);
  for (int i = 1; i < vertex_count; ++i) {
    auto v = dba.InsertVertex();
    std::uniform_int_distribution<> dis(0U, vertices.size() - 1U);
    auto &parent = vertices.at(dis(rg));
    dba.InsertEdge(parent, v, dba.EdgeType("Type"));
    vertices.push_back(v);
  }
  dba.Commit();
}

static query::CypherQuery *ParseCypherQuery(const std::string &query_string,
                                            query::AstStorage *ast) {
  query::frontend::ParsingContext parsing_context;
  parsing_context.is_query_cached = false;
  query::frontend::opencypher::Parser parser(query_string);
  // Convert antlr4 AST into Memgraph AST.
  query::frontend::CypherMainVisitor cypher_visitor(parsing_context, ast);
  cypher_visitor.visit(parser.tree());
  query::Interpreter::ParsedQuery parsed_query{
      cypher_visitor.query(),
      query::GetRequiredPrivileges(cypher_visitor.query())};
  return utils::Downcast<query::CypherQuery>(parsed_query.query);
};

template <class TMemory>
// NOLINTNEXTLINE(google-runtime-references)
static void Distinct(benchmark::State &state) {
  query::AstStorage ast;
  query::Parameters parameters;
  database::GraphDb db;
  AddVertices(&db, state.range(0));
  auto dba = db.Access();
  auto query_string = "MATCH (s) RETURN DISTINCT s";
  auto *cypher_query = ParseCypherQuery(query_string, &ast);
  auto symbol_table = query::MakeSymbolTable(cypher_query);
  auto context =
      query::plan::MakePlanningContext(&ast, &symbol_table, cypher_query, &dba);
  auto plan_and_cost =
      query::plan::MakeLogicalPlan(&context, parameters, false);
  ResultStreamFaker<query::TypedValue> results;
  query::Frame frame(symbol_table.max_position());
  // Nothing should be used from the EvaluationContext, so leave it empty.
  query::EvaluationContext evaluation_context;
  while (state.KeepRunning()) {
    query::ExecutionContext execution_context{&dba, symbol_table,
                                              evaluation_context};
    TMemory memory;
    auto cursor = plan_and_cost.first->MakeCursor(&dba, memory.get());
    while (cursor->Pull(frame, execution_context))
      ;
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(Distinct, NewDeleteResource)
    ->Range(1024, 1U << 21U)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(Distinct, MonotonicBufferResource)
    ->Range(1024, 1U << 21U)
    ->Unit(benchmark::kMicrosecond);

static query::plan::ExpandVariable MakeExpandVariable(
    query::EdgeAtom::Type expand_type, query::SymbolTable *symbol_table) {
  auto input_symbol = symbol_table->CreateSymbol("input", false);
  auto dest_symbol = symbol_table->CreateSymbol("dest", false);
  auto edge_symbol = symbol_table->CreateSymbol("edge", false);
  auto lambda_node_symbol = symbol_table->CreateSymbol("n", false);
  auto lambda_edge_symbol = symbol_table->CreateSymbol("e", false);
  query::plan::ExpansionLambda filter_lambda;
  filter_lambda.inner_node_symbol = lambda_node_symbol;
  filter_lambda.inner_edge_symbol = lambda_edge_symbol;
  filter_lambda.expression = nullptr;
  return query::plan::ExpandVariable(
      nullptr, input_symbol, dest_symbol, edge_symbol, expand_type,
      query::EdgeAtom::Direction::OUT, {}, false, nullptr, nullptr, false,
      filter_lambda, std::nullopt, std::nullopt);
}

template <class TMemory>
// NOLINTNEXTLINE(google-runtime-references)
static void ExpandVariable(benchmark::State &state) {
  query::AstStorage ast;
  query::Parameters parameters;
  database::GraphDb db;
  AddStarGraph(&db, state.range(0), state.range(1));
  query::SymbolTable symbol_table;
  auto expand_variable =
      MakeExpandVariable(query::EdgeAtom::Type::DEPTH_FIRST, &symbol_table);
  auto dba = db.Access();
  query::Frame frame(symbol_table.max_position());
  // Nothing should be used from the EvaluationContext, so leave it empty.
  query::EvaluationContext evaluation_context;
  while (state.KeepRunning()) {
    query::ExecutionContext execution_context{&dba, symbol_table,
                                              evaluation_context};
    TMemory memory;
    auto cursor = expand_variable.MakeCursor(&dba, memory.get());
    for (const auto &v : dba.Vertices(dba.Label(kStartLabel), false)) {
      frame[expand_variable.input_symbol_] = v;
      while (cursor->Pull(frame, execution_context))
        ;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(ExpandVariable, NewDeleteResource)
    ->Ranges({{1, 1U << 5U}, {512, 1U << 13U}})
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(ExpandVariable, MonotonicBufferResource)
    ->Ranges({{1, 1U << 5U}, {512, 1U << 13U}})
    ->Unit(benchmark::kMicrosecond);

template <class TMemory>
// NOLINTNEXTLINE(google-runtime-references)
static void ExpandBfs(benchmark::State &state) {
  query::AstStorage ast;
  query::Parameters parameters;
  database::GraphDb db;
  AddTree(&db, state.range(0));
  query::SymbolTable symbol_table;
  auto expand_variable =
      MakeExpandVariable(query::EdgeAtom::Type::BREADTH_FIRST, &symbol_table);
  auto dba = db.Access();
  query::Frame frame(symbol_table.max_position());
  // Nothing should be used from the EvaluationContext, so leave it empty.
  query::EvaluationContext evaluation_context;
  while (state.KeepRunning()) {
    query::ExecutionContext execution_context{&dba, symbol_table,
                                              evaluation_context};
    TMemory memory;
    auto cursor = expand_variable.MakeCursor(&dba, memory.get());
    for (const auto &v : dba.Vertices(dba.Label(kStartLabel), false)) {
      frame[expand_variable.input_symbol_] = v;
      while (cursor->Pull(frame, execution_context))
        ;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(ExpandBfs, NewDeleteResource)
    ->Range(512, 1U << 19U)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(ExpandBfs, MonotonicBufferResource)
    ->Range(512, 1U << 19U)
    ->Unit(benchmark::kMicrosecond);

template <class TMemory>
// NOLINTNEXTLINE(google-runtime-references)
static void ExpandShortest(benchmark::State &state) {
  query::AstStorage ast;
  query::Parameters parameters;
  database::GraphDb db;
  AddTree(&db, state.range(0));
  query::SymbolTable symbol_table;
  auto expand_variable =
      MakeExpandVariable(query::EdgeAtom::Type::BREADTH_FIRST, &symbol_table);
  expand_variable.common_.existing_node = true;
  auto dest_symbol = expand_variable.common_.node_symbol;
  auto dba = db.Access();
  query::Frame frame(symbol_table.max_position());
  // Nothing should be used from the EvaluationContext, so leave it empty.
  query::EvaluationContext evaluation_context;
  while (state.KeepRunning()) {
    query::ExecutionContext execution_context{&dba, symbol_table,
                                              evaluation_context};
    TMemory memory;
    auto cursor = expand_variable.MakeCursor(&dba, memory.get());
    for (const auto &v : dba.Vertices(dba.Label(kStartLabel), false)) {
      frame[expand_variable.input_symbol_] = v;
      for (const auto &dest : dba.Vertices(false)) {
        frame[dest_symbol] = dest;
        while (cursor->Pull(frame, execution_context))
          ;
      }
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(ExpandShortest, NewDeleteResource)
    ->Range(512, 1U << 20U)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(ExpandShortest, MonotonicBufferResource)
    ->Range(512, 1U << 20U)
    ->Unit(benchmark::kMicrosecond);

template <class TMemory>
// NOLINTNEXTLINE(google-runtime-references)
static void Accumulate(benchmark::State &state) {
  query::AstStorage ast;
  query::Parameters parameters;
  database::GraphDb db;
  AddVertices(&db, state.range(1));
  query::SymbolTable symbol_table;
  auto scan_all = std::make_shared<query::plan::ScanAll>(
      nullptr, symbol_table.CreateSymbol("v", false));
  std::vector<query::Symbol> symbols;
  symbols.reserve(state.range(0));
  for (int i = 0; i < state.range(0); ++i) {
    symbols.push_back(symbol_table.CreateSymbol(std::to_string(i), false));
  }
  query::plan::Accumulate accumulate(scan_all, symbols,
                                     /* advance_command= */ false);
  auto dba = db.Access();
  query::Frame frame(symbol_table.max_position());
  // Nothing should be used from the EvaluationContext, so leave it empty.
  query::EvaluationContext evaluation_context;
  while (state.KeepRunning()) {
    query::ExecutionContext execution_context{&dba, symbol_table,
                                              evaluation_context};
    TMemory memory;
    auto cursor = accumulate.MakeCursor(&dba, memory.get());
    while (cursor->Pull(frame, execution_context))
      ;
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(Accumulate, NewDeleteResource)
    ->Ranges({{4, 1U << 7U}, {512, 1U << 13U}})
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_TEMPLATE(Accumulate, MonotonicBufferResource)
    ->Ranges({{4, 1U << 7U}, {512, 1U << 13U}})
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();