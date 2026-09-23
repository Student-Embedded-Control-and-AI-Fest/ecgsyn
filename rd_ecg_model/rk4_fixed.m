function [t,y] = rk4_fixed(odefun,t0,tf,x0,dt)
  t = t0:dt:tf;
  n = numel(t);
  y = zeros(numel(x0), n);
  y(:, 1) = x0';
  for k = 1:n-1
    tk = t(k); yk = y(:, k);
    k1 = odefun(tk,      yk);
    k2 = odefun(tk+dt/2, yk + dt/2*k1);
    k3 = odefun(tk+dt/2, yk + dt/2*k2);
    k4 = odefun(tk+dt,   yk + dt*k3);
    y(:, k+1) = yk + dt/6*(k1 + 2*k2 + 2*k3 + k4);
  end
end
