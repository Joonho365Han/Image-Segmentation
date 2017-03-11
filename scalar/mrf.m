%
%   Joonho Han (joonho18)
%   Problem #1
%

rng('default');
Exp = 128;
Std = 50;
Uorigin = double(uint8(randn(130)*Std + Exp));

for T = 1:2
    
    figure(T);
    subplot(3,2,1);
    imshow(uint8(Uorigin));
    title('Original U');
    
    %% 1st Order Icing Potential 
    Uresult = Uorigin;
    for iv = 1:5
        U = Uresult;
        for j = 2:129
            for i = 2:129
                % Gibbs Probability for each random variable at (j,i)
                L = linspace(0,255,256);
                % Energy of q for j,i
                Eq = zeros(1,256);
                % Ising Potential function (V)
                Eq = Eq + 5*(L ~= U(j-1,i));
                Eq = Eq + 5*(L ~= U(j,i-1));
                Eq = Eq + 5*(L ~= U(j+1,i));
                Eq = Eq + 5*(L ~= U(j,i+1));
                P = exp(-Eq./T);

                F = cumsum(P)./sum(P);
                n = find(F>rand,1);
                Uresult(i,j) = n;
            end
        end
    end
    
    subplot(3,2,3);
    imshow(uint8(Uresult));
    title('1st Order Icing Potential');

    %% 2nd Order Icing Potential 
    Uresult = Uorigin;
    for iv = 1:5
        U = Uresult;
        for j = 2:129
            for i = 2:129
                % Gibbs Probability for each random variable at (j,i)
                L = linspace(0,255,256);
                % Energy of q for j,i
                Eq = zeros(1,256);
                % Ising Potential function (V)
                Eq = Eq + 5*(L ~= U(j-1,i));
                Eq = Eq + 5*(L ~= U(j,i-1));
                Eq = Eq + 5*(L ~= U(j+1,i));
                Eq = Eq + 5*(L ~= U(j,i+1));
                Eq = Eq + 5*(L ~= U(j-1,i+1));
                Eq = Eq + 5*(L ~= U(j-1,i-1));
                Eq = Eq + 5*(L ~= U(j+1,i-1));
                Eq = Eq + 5*(L ~= U(j+1,i+1));
                P = exp(-Eq./T);

                F = cumsum(P)./sum(P);
                n = find(F>rand,1);
                Uresult(i,j) = n;
            end
        end
    end
    
    subplot(3,2,4);
    imshow(uint8(Uresult));
    title('2nd Order Icing Potential');

    %% 1st Order Absolute Potential 
    Uresult = Uorigin;
    for iv = 1:5
        U = Uresult;
        for j = 2:129
            for i = 2:129
                % Gibbs Probability for each random variable at (j,i)
                L = linspace(0,255,256);
                % Energy of q for (j,i) and
                % the Absolute Potential function
                Eq = abs(L-U(j-1,i))+abs(L-U(j,i-1))+abs(L-U(j+1,i))+abs(L-U(j,i+1));
                P = exp(double(-Eq./T));

                F = cumsum(P)./sum(P);
                n = find(F>rand,1);
                Uresult(i,j) = n;
            end
        end
    end
    
    subplot(3,2,5);
    imshow(uint8(Uresult));
    title('1st Order Absolute Potential');

    %% 2nd Order Absolute Potential 
    Uresult = Uorigin;
    for iv = 1:5
        U = Uresult;
        for j = 2:129
            for i = 2:129
                % Gibbs Probability for each random variable at (j,i)
                L = linspace(0,255,256);
                % Energy of q for (j,i) and
                % the Absolute Potential function
                Eq = abs(L-U(j-1,i))+abs(L-U(j,i-1))+abs(L-U(j+1,i));
                Eq=Eq +abs(L-U(j,i+1))+abs(L-U(j-1,i-1))+abs(L-U(j-1,i+1));
                Eq=Eq +abs(L-U(j+1,i-1))+abs(L-U(j+1,i+1));
                P = exp(-Eq./T);

                F = cumsum(P)./sum(P);
                n = find(F>rand,1);
                Uresult(i,j) = n;
            end
        end
    end
    
    subplot(3,2,6);
    imshow(uint8(Uresult));
    title('2nd Order Absolute Potential');
end