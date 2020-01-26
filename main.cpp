#include "panic.h"

#include "shaderprogram.h"
#include "spritebatcher.h"
#include "texture.h"
#include "geometry.h"
#include "trajectory.h"
#include "level.h"
#include "tilesheet.h"
#include "dpadstate.h"
#include "world.h"
#include "font.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <boost/asio.hpp>

#include <cassert>
#include <array>
#include <vector>
#include <algorithm>
#include <tuple>
#include <memory>
#include <thread>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <queue>

#define DRAW_FRAMES

SpriteBatcher *g_sprite_batcher;
unsigned g_dpad_state = 0;

static constexpr const auto ViewportWidth = 400;
static constexpr const auto ViewportHeight = 600;
static constexpr const auto ViewportMargin = 12;
static constexpr const auto WindowWidth = 2 * ViewportWidth + 3 * ViewportMargin;
static constexpr const auto WindowHeight = ViewportHeight + 2 * ViewportMargin;

static constexpr const auto TicsPerSecond = 60;
static constexpr const auto MillisecondsPerTic = 1000.0f / TicsPerSecond;

static constexpr auto ServerPort = 4141;

template <typename T>
class Queue
{
public:
    void push(const T &value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            data_.push(value);
        }
        not_empty_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this]() {
            return !data_.empty();
        });

        assert(!data_.empty());
        const auto result = data_.front();
        data_.pop();

        return result;
    }

private:
    std::mutex mutex_;
    std::queue<T> data_;
    std::condition_variable not_empty_;
};

using Message = unsigned;

class NetworkThread
{
public:
    NetworkThread()
        : socket_(io_context_)
    {
    }

    ~NetworkThread()
    {
        io_context_.stop(); // XXX can we actually do this?
        thread_.join();
    }

    void start()
    {
        do_connect(); // XXX probably should do this without inheritance
        thread_ = std::thread([this] { io_context_.run(); });
    }

    void write_message(const Message &message)
    {
        io_context_.post(
            [this, message]
            {
                boost::system::error_code ignored_error;
                boost::asio::write(socket_, boost::asio::buffer(&message, sizeof(message)), ignored_error);
            });
    }

    Message read_remote_message()
    {
        return read_queue_.pop();
    }

    enum class Status
    {
        Connecting,
        Connected,
        Disconnected,
    };

    Status status() const { return status_; }

protected:
    virtual void do_connect() = 0;

    void handle_connected(boost::system::error_code ec)
    {
        if (!ec)
        {
            status_ = Status::Connected;

            boost::asio::ip::tcp::no_delay option(true);
            socket_.set_option(option);

            do_read();
        }
        else
        {
            status_ = Status::Disconnected;
        }
    }

    void do_read()
    {
        boost::asio::async_read(socket_,
            boost::asio::buffer(&read_message_, sizeof(read_message_)),
            [this](boost::system::error_code ec, std::size_t bytes_transferred)
            {
                if (!ec)
                {
                    read_queue_.push(read_message_);
                    do_read();
                }
                else
                {
                    socket_.close();
                    status_ = Status::Disconnected;
                    read_queue_.push(0); // to unblock anyone reading the queue
                }
            });
    }

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::socket socket_;
    std::thread thread_;
    Message read_message_;
    Queue<Message> read_queue_;
    Status status_ = Status::Connecting;
};

class ServerNetworkThread : public NetworkThread
{
public:
    ServerNetworkThread(int port)
        : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
    }

private:
    void do_connect() override
    {
        acceptor_.async_accept(socket_,
            [this](boost::system::error_code ec)
            {
                handle_connected(ec);
            });
    }

    boost::asio::ip::tcp::acceptor acceptor_;
};

class ClientNetworkThread : public NetworkThread
{
public:
    ClientNetworkThread(std::string_view host, std::string_view service)
    {
        boost::asio::ip::tcp::resolver resolver(io_context_);
        endpoint_iterator_ = resolver.resolve(host, service);
    }

private:
    void do_connect() override
    {
        boost::asio::async_connect(socket_, endpoint_iterator_,
            [this](boost::system::error_code ec, boost::asio::ip::tcp::resolver::iterator)
            {
                handle_connected(ec);
            });
    }

    boost::asio::ip::tcp::resolver::iterator endpoint_iterator_;
};

class Game
{
public:
    enum class NetworkMode
    {
        Client,
        Server
    };

    Game(NetworkMode mode, const std::string &host);

    bool advance(float dt);
    void render() const;

private:
    void advance_one_tic();

    int window_width_;
    int window_height_;
    std::unique_ptr<Level> level_;
    World local_;
    World remote_;
#ifdef DRAW_FRAMES
    ShaderProgram frame_program_;
    Geometry<std::tuple<glm::vec2>> frame_;
#endif
    float timestamp_ = 0.0f; // milliseconds
    std::unique_ptr<NetworkThread> network_thread_;
};

Game::Game(NetworkMode mode, const std::string &host)
    : level_(load_level("resources/levels/level-0.json"))
    , local_(ViewportWidth, ViewportHeight)
    , remote_(ViewportWidth, ViewportHeight)
{
    local_.initialize_level(level_.get());
    remote_.initialize_level(level_.get());

#ifdef DRAW_FRAMES
    constexpr float x0 = 0;
    constexpr float x1 = ViewportWidth;
    constexpr float y0 = 0;
    constexpr float y1 = ViewportHeight;

    static const std::vector<std::tuple<glm::vec2>> frame_verts =
        {{{x0, y0}}, {{x1, y0}}, {{x1, y1}}, {{x0, y1}}};
    frame_.set_data(frame_verts);

    frame_program_.add_shader(GL_VERTEX_SHADER, "resources/shaders/dummy.vert");
    frame_program_.add_shader(GL_FRAGMENT_SHADER, "resources/shaders/dummy.frag");
    frame_program_.link();
#endif

    if (mode == NetworkMode::Server)
        network_thread_.reset(new ServerNetworkThread(ServerPort));
    else
        network_thread_.reset(new ClientNetworkThread(host, std::to_string(ServerPort)));
    network_thread_->start();
}

bool Game::advance(float dt)
{
    const auto status = network_thread_->status();
    if (status == NetworkThread::Status::Disconnected)
        return false;

    if (status == NetworkThread::Status::Connecting)
        return true;

    timestamp_ += dt;
    while (timestamp_ > MillisecondsPerTic)
    {
        timestamp_ -= MillisecondsPerTic;
        advance_one_tic();
    }

    return true;
}

void Game::advance_one_tic()
{
    network_thread_->write_message(g_dpad_state);
    local_.advance(g_dpad_state);

    const auto remote_dpad_state = network_thread_->read_remote_message();
    remote_.advance(remote_dpad_state);
}

void Game::render() const
{
    const auto project =
        glm::ortho(0.0f, static_cast<float>(WindowWidth), static_cast<float>(WindowHeight), 0.0f);

    glEnable(GL_SCISSOR_TEST);

    const auto draw_viewport = [this, &project](const World &world, int x_offset) {
        const auto translate
            = glm::translate(glm::mat4(1.0f), glm::vec3(x_offset, ViewportMargin, 0.0f));

        glScissor(x_offset, ViewportMargin, ViewportWidth, ViewportHeight);

        g_sprite_batcher->set_transform_matrix(project * translate);
        g_sprite_batcher->start_batch();
        world.render();
        g_sprite_batcher->render_batch();

#ifdef DRAW_FRAMES
        glDisable(GL_SCISSOR_TEST);

        const auto mvp = frame_program_.uniform_location("mvp");
        frame_program_.bind();
        frame_program_.set_uniform(mvp, project * translate);
        frame_.render(GL_LINE_LOOP);

        glEnable(GL_SCISSOR_TEST);
#endif
    };

    draw_viewport(local_, ViewportMargin);
    draw_viewport(remote_, 2 * ViewportMargin + ViewportWidth);

    if (network_thread_->status() == NetworkThread::Status::Connecting)
    {
        const auto translate
            = glm::translate(glm::mat4(1.0f),
                             glm::vec3(2 * ViewportMargin + ViewportWidth, ViewportMargin, 0.0f));
        render_text(project * translate, 0.5f * ViewportWidth, 0.5f * ViewportHeight, "WAITING FOR PLAYER");
    }

    glDisable(GL_SCISSOR_TEST);
}

static void update_dpad_state(GLFWwindow *window)
{
    unsigned state = 0;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        state |= DPad_Up;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        state |= DPad_Down;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        state |= DPad_Left;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        state |= DPad_Right;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        state |= DPad_Button;
    g_dpad_state = state;
}

int main(int argc, char *argv[])
{
    std::string host;
    Game::NetworkMode mode;
    if (argc == 1)
    {
        mode = Game::NetworkMode::Server;
    }
    else
    {
        mode = Game::NetworkMode::Client;
        host = argv[1];
    }

    if (!glfwInit())
        panic("glfwInit failed\n");

    glfwSetErrorCallback([](int error, const char *description) { panic("GLFW error: %s\n", description); });

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 16);
    auto *window = glfwCreateWindow(WindowWidth, WindowHeight, "demo", nullptr, nullptr);
    if (!window)
        panic("glfwCreateWindow failed\n");

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewInit();

    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mode) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GL_TRUE);
    });

    glViewport(0, 0, WindowWidth, WindowHeight);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    {
        cache_tilesheet("resources/tilesheets/sheet.json");
        g_sprite_batcher = new SpriteBatcher;

        {
            Game game(mode, host);

            while (!glfwWindowShouldClose(window))
            {
                update_dpad_state(window);

                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);

                if (!game.advance(1000.f / 60.f))
                    break;

                game.render();

                glfwSwapBuffers(window);
                glfwPollEvents();
            }
        }

        delete g_sprite_batcher;
        release_tilesheets();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
