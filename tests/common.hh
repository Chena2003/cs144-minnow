#pragma once

#include "conversions.hh"
#include "exception.hh"

#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

// 异常捕获
class ExpectationViolation : public std::runtime_error
{
public:
  static constexpr std::string boolstr( bool b ) { return b ? "true" : "false"; }

  explicit ExpectationViolation( const std::string& msg ) : std::runtime_error( msg ) {}

  template<typename T>
  inline ExpectationViolation( const std::string& property_name, const T& expected, const T& actual );
};

// 构造函数通用模板
template<typename T>
ExpectationViolation::ExpectationViolation( const std::string& property_name, const T& expected, const T& actual )
  : ExpectationViolation { "The object should have had " + property_name + " = " + to_string( expected )
                           + ", but instead it was " + to_string( actual ) + "." }
{}

// 模板特例化，对bool类型生成专门的构造函数
template<>
inline ExpectationViolation::ExpectationViolation( const std::string& property_name,
                                                   const bool& expected,
                                                   const bool& actual )
  : ExpectationViolation { "The object should have had " + property_name + " = " + boolstr( expected )
                           + ", but instead it was " + boolstr( actual ) + "." }
{}

// 动作基类
template<class T>
struct TestStep
{
  /*
  动作的描述
  Expectation类输出为: "Expectation: " +
  description()。但在ExpectNumber中对description做了实现，在子类中只需要重写name()函数即可（也可以重写description()函数）
  Action类输出为: "Action: " + description()。每个子类重写description()函数，做定制化说明
  */
  virtual std::string str() const = 0;

  /*
  执行动作
  Expectation类execute具体的实现在ExpectNumber和ConstExpectNumber中定义。具体来说，Expectation类execute实现中通过调用子类中的value函数得到结果，与预期结果对比。如果对比失败，则触发throw
  ExpectationViolation { name(), value_, result
  }，从而在TestHarness的execute函数中被捕获，输出相应的信息。因此，在Expectation子类中需要实现value(obj_)执行相关动作，本质上就是obj_->action().
  Action类execute具体实现在每个子类中，本质就是execute(obj_)中调用obj_->action()执行相关动作
  */
  virtual void execute( T& ) const = 0;

  /*
  输出颜色
  Expectation类输出为绿色
  Action类输出为蓝色
  */
  virtual uint8_t color() const = 0;
  virtual ~TestStep() = default;
};

// 控制输出类，实现输出带颜色，是否终端输出
class Printer
{
  bool is_terminal_;

public:
  Printer();

  static constexpr int red = 31;
  static constexpr int green = 32;
  static constexpr int blue = 34;
  static constexpr int def = 39;

  // 返回带有color_value颜色的string
  std::string with_color( int color_value, std::string_view str ) const;

  // 美化输出样式
  static std::string prettify( std::string_view str, size_t max_length = 32 );

  // 输出诊断信息，包括测试名字，测试执行动作序列，失败动作和捕获的异常
  void diagnostic( std::string_view test_name,
                   const std::vector<std::pair<std::string, int>>& steps_executed,
                   const std::string& failing_step,
                   const std::exception& e ) const;
};

// 测试类
template<class T>
class TestHarness
{
  std::string test_name_; // 测试名字
  T obj_;                 // 测试对象

  std::vector<std::pair<std::string, int>> steps_executed_ {}; // 执行动作序列
  Printer pr_ {};                                              // 输出控制

protected:
  explicit TestHarness( std::string test_name, std::string_view desc, T&& object )
    : test_name_( std::move( test_name ) ), obj_( std::move( object ) )
  {
    steps_executed_.emplace_back( "Initialized " + demangle( typeid( T ).name() ) + " with " + std::string { desc },
                                  Printer::def );
  }

  const T& object() const { return obj_; }

public:
  void execute( const TestStep<T>& step ) // 测试函数
  {
    try {
      step.execute( obj_ );                                     // 传入obj_对象，通过obj_对象执行step，进行测试
      steps_executed_.emplace_back( step.str(), step.color() ); // 记录测试动作
    } catch ( const ExpectationViolation& e ) {                 // 测试失败
      pr_.diagnostic( test_name_, steps_executed_, step.str(), e );
      throw std::runtime_error { "The test \"" + test_name_ + "\" failed." };
    } catch ( const std::exception& e ) { // 其他报错
      pr_.diagnostic( test_name_, steps_executed_, step.str(), e );
      throw std::runtime_error { "The test \"" + test_name_ + "\" made your code throw an exception." };
    }
  }
};

// 比较类
template<class T>
struct Expectation : public TestStep<T>
{
  std::string str() const override { return "Expectation: " + description(); }
  virtual std::string description() const = 0;
  uint8_t color() const override { return Printer::green; }
};

// 动作类
template<class T>
struct Action : public TestStep<T>
{
  std::string str() const override { return "Action: " + description(); }
  virtual std::string description() const = 0;
  uint8_t color() const override { return Printer::blue; }
};

// 比较数字
template<class T, typename Num>
struct ExpectNumber : public Expectation<T>
{
  Num value_;
  explicit ExpectNumber( Num value ) : value_( value ) {}
  std::string description() const override
  {
    // if constexpr 删除条件为假的分支，避免编译失败
    if constexpr ( std::is_same<Num, bool>::value ) {
      return name() + " = " + ExpectationViolation::boolstr( value_ );
    } else {
      return name() + " = " + to_string( value_ );
    }
  }
  virtual std::string name() const = 0;
  virtual Num value( T& ) const = 0;
  void execute( T& obj ) const override
  {
    const Num result { value( obj ) };
    if ( result != value_ ) {
      throw ExpectationViolation { name(), value_, result };
    }
  }
};

// Const比较类
template<class T, typename Num>
struct ConstExpectNumber : public ExpectNumber<T, Num>
{
  using ExpectNumber<T, Num>::ExpectNumber;
  using ExpectNumber<T, Num>::execute;
  using ExpectNumber<T, Num>::name;
  void execute( const T& obj ) const
  {
    const Num result { value( obj ) };
    if ( result != ExpectNumber<T, Num>::value_ ) {
      throw ExpectationViolation { name(), ExpectNumber<T, Num>::value_, result };
    }
  }
  Num value( T& obj ) const override { return value( std::as_const( obj ) ); }
  virtual Num value( const T& ) const = 0;
};

// 比较bool类型
template<class T>
struct ExpectBool : public ExpectNumber<T, bool>
{
  using ExpectNumber<T, bool>::ExpectNumber;
};

// 比较bool类const
template<class T>
struct ConstExpectBool : public ConstExpectNumber<T, bool>
{
  using ConstExpectNumber<T, bool>::ConstExpectNumber;
};
