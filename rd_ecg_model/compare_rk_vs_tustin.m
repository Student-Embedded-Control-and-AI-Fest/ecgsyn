clear; close all; clc;

% --- Constants ---
C = 1.35;
beta = 4.0;
alpha = [-0.024, 0.0216, -0.0012, 0.12];

dt = 0.01;
tspan = [0 300];
t = tspan(1):dt:tspan(2);

HR_bpm = 90;
Gamma_t = 0.08804 * HR_bpm - 0.06754;

H_values = [3.0, 2.729, 2.164];
titles = {'Normal Rhythm', 'Quasiperiodicity', 'VF'};

x0 = [0; 0; 0.1; 0];

figure('Position',[100 100 1000 900]);
tl = tiledlayout(3,2,'TileSpacing','compact','Padding','compact');

for i = 1:3
    H = H_values(i);

    % --- ODE definition ---
    ode_fun = @(t, x) Gamma_t * [ ...
        x(1) - x(2) - C*x(1)*x(2) - x(1)*x(2)^2;
        H*x(1) - 3*x(2) + C*x(1)*x(2) + x(1)*x(2)^2 + beta*(x(4) - x(2));
        x(3) - x(4) - C*x(3)*x(4) - x(3)*x(4)^2;
        H*x(3) - 3*x(4) + C*x(3)*x(4) + x(3)*x(4)^2 + 2*beta*(x(2) - x(4))
    ];

    % =========================
    % RK4 (Explicit)
    % =========================
    [t_rk, x_rk] = rk4_fixed(ode_fun, tspan(1), tspan(2), x0, dt);
    x_rk = x_rk';
    ECG_rk = alpha * x_rk';

    % Detect failure
    fail_idx = find(~isfinite(ECG_rk) | abs(ECG_rk) > 10, 1);
    if isempty(fail_idx)
        fail_time = NaN;
    else
        fail_time = t_rk(fail_idx);
    end

    % =========================
    % Implicit Tustin
    % =========================
    n_steps = length(t);
    x = zeros(4, n_steps);
    x(:,1) = x0;

    for n = 1:n_steps-1
        xp = x(:, n);
        xn = xp;

        for iter = 1:10
            f_n = ode_fun(0, xn);
            f_p = ode_fun(0, xp);

            R = xn - xp - (dt/2)*(f_n + f_p);

            % Jacobian (same as your code)
            J = eye(4) - (dt/2)*Gamma_t * [ ...
                (1 - C*xn(2) - xn(2)^2), (-1 - C*xn(1) - 2*xn(1)*xn(2)), 0, 0;
                (H + C*xn(2) + xn(2)^2), (-3 + C*xn(1) + 2*xn(1)*xn(2) - beta), 0, beta;
                0, 0, (1 - C*xn(4) - xn(4)^2), (-1 - C*xn(3) - 2*xn(3)*xn(4));
                0, 2*beta, (H + C*xn(4) + xn(4)^2), (-3 + C*xn(3) + 2*xn(3)*xn(4) - 2*beta)
            ];

            dx = J \ R;
            xn = xn - 0.5*dx;

            if norm(dx) < 1e-8
                break;
            end
        end

        x(:,n+1) = xn;
    end

    ECG_tustin = alpha * x;

    % =========================
    % Plot
    % =========================

    % RK (left column)
    nexttile;
    plot(t_rk, ECG_rk, 'k', 'LineWidth', 1.5);
    hold on;

    if ~isnan(fail_time)
        xline(fail_time, 'r--', 'LineWidth', 1.5);
        text(fail_time, 0.8, 'RK4 Failure', ...
            'Color','r','FontSize',10);
    end

    xlim([25 40]); ylim([-1 1]);
    title([titles{i}, ' (RK4)']);
    ylabel('ECG');
    grid on;

    % Tustin (right column)
    nexttile;
    plot(t, ECG_tustin, 'k', 'LineWidth', 1.5);
    xlim([25 40]); ylim([-1 1]);
    title([titles{i}, ' (Implicit Tustin)']);
    grid on;

end

xlabel('Time (s)');